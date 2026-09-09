#include "stdafx.h"
#include "bgfxVideoDecoder.h"

#include <cstring>
#include <cstdio>

BgfxVideoDecoder::BgfxVideoDecoder()
    : m_reader(nullptr)
    , m_serial(0)
    , m_thDecoder(nullptr)
    , m_thSetup(nullptr)
    , m_opened(false)
    , m_playing(false)
    , m_headerDone(false)
    , m_ended(false)
    , m_width(0)
    , m_height(0)
    , m_duration(0.0)
    , m_frameCount(0)
    , m_texture(BGFX_INVALID_HANDLE)
{
    m_path[0] = 0;
    memset(&m_oggSync, 0, sizeof(m_oggSync));
    memset(&m_oggStream, 0, sizeof(m_oggStream));
    memset(&m_oggPage, 0, sizeof(m_oggPage));
    memset(&m_oggPacket, 0, sizeof(m_oggPacket));
    memset(&m_thInfo, 0, sizeof(m_thInfo));
    memset(&m_thComment, 0, sizeof(m_thComment));
}

BgfxVideoDecoder::~BgfxVideoDecoder()
{
    Close();
}

bool BgfxVideoDecoder::Open(const char* path)
{
    // Reset the decoding state but keep the bgfx texture/buffer alive
    // (reused by Restart to avoid a black flash when looping).
    ResetStream();

    if (path && path[0])
    {
        strncpy_s(m_path, path, sizeof(m_path) - 1);
        m_ended = false;
    }

    // Try the raw path first, then fall back to the virtual filesystem
    // ($game_textures$ = <fs_root>\gamedata\textures\). The UI passes
    // paths like "textures\intro\intro_gsc", so strip the "textures\" prefix
    // and resolve against $game_textures$.
    if (path && path[0])
        m_reader = FS.r_open(path);
    if (!m_reader && path && path[0])
    {
        const char* fsPath = path;
        static const char textures_prefix[] = "textures\\";
        const size_t prefixLen = sizeof(textures_prefix) - 1;
        if (0 == _strnicmp(path, textures_prefix, prefixLen))
            fsPath = path + prefixLen;
        else if (0 == _strnicmp(path, "$game_textures$\\", 16))
            fsPath = path + 16;

        m_reader = FS.r_open("$game_textures$", fsPath);
    }

    if (!m_reader)
    {
        LogError("[BGFX] VideoDecoder: failed to open '%s'", path);
        return false;
    }

    ogg_sync_init(&m_oggSync);
    th_comment_init(&m_thComment);
    th_info_init(&m_thInfo);

    m_serial = 0;
    m_headerDone = false;
    m_width = 0;
    m_height = 0;
    m_duration = 0.0;

    int headerCount = 0;
    int rejectedSerials[16];
    int numRejected = 0;

    // Read Theora headers. The first stream in the file is not necessarily
    // Theora (e.g. Ogg Skeleton "fishead" or Vorbis audio) — lock onto the
    // stream whose first packet is a Theora header.
    while (!m_headerDone && headerCount < 10)
    {
        int remaining = m_reader->elapsed();
        if (remaining <= 0)
            break;

        size_t bytes = (remaining > 4096) ? 4096 : remaining;
        char* buffer = ogg_sync_buffer(&m_oggSync, (long)bytes);
        m_reader->r(buffer, (int)bytes);
        ogg_sync_wrote(&m_oggSync, (long)bytes);

        while (ogg_sync_pageout(&m_oggSync, &m_oggPage) == 1)
        {
            int serial = ogg_page_serialno(&m_oggPage);

            bool rejected = false;
            for (int i = 0; i < numRejected; i++)
            {
                if (rejectedSerials[i] == serial)
                {
                    rejected = true;
                    break;
                }
            }
            if (rejected)
                continue;

            if (m_serial == 0)
            {
                ogg_stream_init(&m_oggStream, serial);
                m_serial = serial;
            }
            else if (serial != m_serial)
            {
                continue; // skip other streams
            }

            ogg_stream_pagein(&m_oggStream, &m_oggPage);

            while (ogg_stream_packetout(&m_oggStream, &m_oggPacket) == 1)
            {
                int ret = th_decode_headerin(&m_thInfo, &m_thComment, &m_thSetup, &m_oggPacket);
                if (ret < 0)
                {
                    // Not a Theora stream (Ogg Skeleton returns
                    // TH_ENOTFORMAT, Vorbis etc. TH_EBADHEADER) — reject it
                    // and keep looking for the Theora stream.
                    ogg_stream_clear(&m_oggStream);
                    m_serial = 0;
                    if (numRejected < 16)
                        rejectedSerials[numRejected++] = serial;
                    break;
                }
                // Positive returns accumulate to 6: info=3, comment=2, setup=1
                headerCount += ret;
                if (headerCount >= 6)
                {
                    m_headerDone = true;
                    break;
                }
            }
            if (m_headerDone)
                break;
        }
    }

    if (!m_headerDone || headerCount < 3)
    {
        LogError("[BGFX] VideoDecoder: failed to read Theora headers (%d)", headerCount);
        Close();
        return false;
    }

    // Initialize decoder
    m_thDecoder = th_decode_alloc(&m_thInfo, m_thSetup);
    if (!m_thDecoder)
    {
        LogError("[BGFX] VideoDecoder: th_decode_alloc failed");
        Close();
        return false;
    }

    m_width = m_thInfo.pic_width;
    m_height = m_thInfo.pic_height;

    LogInfo("[BGFX] VideoDecoder: opened %s (%dx%d, %.2f fps)",
        path, m_width, m_height,
        (float)m_thInfo.fps_numerator / m_thInfo.fps_denominator);

    // Create bgfx texture (dynamic, RGBA8) at the exact frame size so UI
    // quads can use full 0..1 UVs. On a Restart the previous texture is
    // reused (same dimensions) to avoid a black flash during the loop.
    if (!bgfxIsValid(m_texture))
    {
        m_texture = bgfx_create_texture_2d(
            (uint16_t)m_width, (uint16_t)m_height, false, 1,
            BGFX_TEXTURE_FORMAT_RGBA8,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
            nullptr, 0);

        if (!bgfxIsValid(m_texture))
        {
            LogError("[BGFX] VideoDecoder: failed to create texture");
            Close();
            return false;
        }

        m_rgbBuffer.resize(m_width * m_height * 4);
    }
    m_opened = true;
    m_playing = false;

    return true;
}

void BgfxVideoDecoder::Close()
{
    ResetStream();

    if (bgfxIsValid(m_texture))
    {
        bgfx_destroy_texture(m_texture);
        m_texture = BGFX_INVALID_HANDLE;
    }

    m_rgbBuffer.clear();
    m_width = 0;
    m_height = 0;
    m_duration = 0.0;
}

void BgfxVideoDecoder::ResetStream()
{
    if (m_thDecoder)
    {
        th_decode_free(m_thDecoder);
        m_thDecoder = nullptr;
    }

    if (m_thSetup)
    {
        th_setup_free(m_thSetup);
        m_thSetup = nullptr;
    }

    if (m_serial != 0)
    {
        ogg_stream_clear(&m_oggStream);
        m_serial = 0;
    }

    ogg_sync_clear(&m_oggSync);
    th_comment_clear(&m_thComment);
    th_info_clear(&m_thInfo);

    if (m_reader)
    {
        FS.r_close(m_reader);
        m_reader = nullptr;
    }

    m_opened = false;
    m_playing = false;
    m_headerDone = false;
    m_frameCount = 0;
}

bool BgfxVideoDecoder::HasTexture() const
{
    return m_opened && bgfxIsValid(m_texture) && m_width > 0;
}

bool BgfxVideoDecoder::DecodeFrame()
{
    if (!m_opened || !m_thDecoder)
        return false;

    // Get more data from file
    int remaining = m_reader->elapsed();
    if (remaining > 0)
    {
        size_t bytes = (remaining > 4096) ? 4096 : remaining;
        char* buffer = ogg_sync_buffer(&m_oggSync, (long)bytes);
        m_reader->r(buffer, (int)bytes);
        ogg_sync_wrote(&m_oggSync, (long)bytes);
    }

    // Try to get a page
    while (ogg_sync_pageout(&m_oggSync, &m_oggPage) == 1)
    {
        int serial = ogg_page_serialno(&m_oggPage);
        if (serial != m_serial)
            continue;

        ogg_stream_pagein(&m_oggStream, &m_oggPage);

        while (ogg_stream_packetout(&m_oggStream, &m_oggPacket) == 1)
        {
            int pres = th_decode_packetin(m_thDecoder, &m_oggPacket, nullptr);
            if (pres == 0 || pres == 1)
            {
                // Frame decoded (1 = duplicate frame, no new output)
                th_ycbcr_buffer yuv;
                th_decode_ycbcr_out(m_thDecoder, yuv);
                UploadFrame(yuv);
                return true;
            }
        }
    }

    return false;
}

bool BgfxVideoDecoder::UploadFrame(th_ycbcr_buffer yuv)
{
    if (m_rgbBuffer.empty())
        return false;

    unsigned int w = m_width;
    unsigned int h = m_height;

    ConvertYUV420ToRGB(yuv, m_rgbBuffer);

    // Update bgfx texture
    const bgfx_memory_t* mem = bgfx_make_ref(m_rgbBuffer.data(), (uint32_t)m_rgbBuffer.size());
    bgfx_update_texture_2d(m_texture, 0, 0, 0, 0, (uint16_t)w, (uint16_t)h, mem, (uint16_t)(w * 4));

    return true;
}

void BgfxVideoDecoder::ConvertYUV420ToRGB(const th_ycbcr_buffer yuv, std::vector<unsigned char>& rgb)
{
    unsigned int w = m_width;
    unsigned int h = m_height;
    unsigned int texW = w;

    const unsigned char* yPlane = yuv[0].data;
    const unsigned char* uPlane = yuv[1].data;
    const unsigned char* vPlane = yuv[2].data;

    int yStride = yuv[0].stride;
    int uvStride = yuv[1].stride;

    for (unsigned int row = 0; row < h; row++)
    {
        for (unsigned int col = 0; col < w; col++)
        {
            int y = yPlane[row * yStride + col];
            int u = uPlane[(row / 2) * uvStride + (col / 2)] - 128;
            int v = vPlane[(row / 2) * uvStride + (col / 2)] - 128;

            // BT.601 conversion
            int r = (int)(y + 1.402 * v);
            int g = (int)(y - 0.344136 * u - 0.714136 * v);
            int b = (int)(y + 1.772 * u);

            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;

        unsigned int idx = (row * texW + col) * 4;
        // BGFX_TEXTURE_FORMAT_RGBA8 = DXGI R8G8B8A8: byte order is R,G,B,A
        rgb[idx + 0] = (unsigned char)r;
        rgb[idx + 1] = (unsigned char)g;
        rgb[idx + 2] = (unsigned char)b;
        rgb[idx + 3] = 255;
        }
    }
}

bgfx_texture_handle_t BgfxVideoDecoder::GetTexture() const
{
    return m_texture;
}

void BgfxVideoDecoder::Seek(double /*time*/)
{
    // Not implemented — intro videos play from start
}

void BgfxVideoDecoder::Play()
{
    m_frameCount = 0;
    m_playing = true;
}

void BgfxVideoDecoder::Stop()
{
    m_playing = false;
}

void BgfxVideoDecoder::Restart()
{
    // Reopen from the start; the bgfx texture is preserved (no black flash).
    if (Open(m_path))
        Play();
}

void BgfxVideoDecoder::Sync(unsigned int timeMs)
{
    if (!m_opened || !m_thDecoder || !m_playing)
        return;

    // The engine passes the audio playback time (or sequence time when
    // there is no sound). Decode exactly the frames whose presentation
    // time has been reached so video stays in sync with the audio.
    double fps = m_thInfo.fps_denominator
        ? (double)m_thInfo.fps_numerator / (double)m_thInfo.fps_denominator
        : 25.0;
    if (fps <= 0.0)
        fps = 25.0;

    ogg_int64_t target = (ogg_int64_t)((double)timeMs * fps / 1000.0);

    // Never rewind (no seek support): only decode forward.
    if (target <= m_frameCount)
        return;

    while (m_frameCount < target)
    {
        // Only treat as the real end of data when the file is fully read;
        // otherwise the failure is a partial page or a stretch of packets
        // from another stream (audio/skeleton) — just retry later.
        bool eof = (m_reader->elapsed() <= 0);
        if (!DecodeFrame())
        {
            if (eof)
            {
                LogInfo("[BGFX] VideoDecoder: data ended at frame=%lld target=%lld", (long long)m_frameCount, (long long)target);
                m_ended = true;
            }
            break;
        }
        ++m_frameCount;
    }
}
