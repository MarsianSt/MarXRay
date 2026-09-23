#include "stdafx.h"
#include "bgfxUIShader.h"
#include "bgfxVideoDecoder.h"

#include <map>
#include <string>

namespace
{
    struct UITextureCacheItem
    {
        bgfx_texture_handle_t texture;
        unsigned int width;
        unsigned int height;
        xr_vector<bgfx_texture_handle_t> seqFrames;
        u32 seqMSPF;
        bool seqCycles;
    };
    std::map<std::string, UITextureCacheItem> g_UITextureCache;

#define DDPF_FOURCC     0x00000004
#define DDPF_RGB        0x00000040
#define DDPF_ALPHA      0x00000002
#define DDSD_MIPMAPCOUNT 0x00020000

#define FOURCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

#pragma pack(push, 1)
    struct DdsPixelFormat
    {
        u32 size;
        u32 flags;
        u32 fourcc;
        u32 rgbBitCount;
        u32 rMask;
        u32 gMask;
        u32 bMask;
        u32 aMask;
    };

    struct DdsHeader
    {
        u32 size;
        u32 flags;
        u32 height;
        u32 width;
        u32 pitchOrLinearSize;
        u32 depth;
        u32 mipMapCount;
        u32 reserved[11];
        DdsPixelFormat ddspf;
        u32 caps;
        u32 caps2;
        u32 caps3;
        u32 caps4;
        u32 reserved2;
    };
#pragma pack(pop)

u32 DdsMipLevelBytes(bgfx_texture_format_t fmt, u32 w, u32 h)
{
    switch (fmt)
    {
    case BGFX_TEXTURE_FORMAT_BC1:
        return ((w + 3) / 4) * ((h + 3) / 4) * 8;
    case BGFX_TEXTURE_FORMAT_BC2:
    case BGFX_TEXTURE_FORMAT_BC3:
        return ((w + 3) / 4) * ((h + 3) / 4) * 16;
    default:
        break;
    }
    return w * h * 4;
}

bgfx_texture_handle_t LoadDDSTexture(IReader* file, unsigned int& outW, unsigned int& outH, bool worldWrap)
{
    u32 size = file->elapsed();
    if (size < 4 + sizeof(DdsHeader))
        return BGFX_INVALID_HANDLE;

    u8* data = (u8*)xr_malloc(size);
    file->r(data, (int)size);

    bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;
    do
    {
        if (data[0] != 'D' || data[1] != 'D' || data[2] != 'S' || data[3] != ' ')
            break;

        DdsHeader hdr;
        memcpy(&hdr, data + 4, sizeof(hdr));

        u32 w = hdr.width;
        u32 h = hdr.height;
        if (w == 0 || h == 0 || (w & 3) || (h & 3))
            break;

        u32 fourcc = hdr.ddspf.fourcc;
        bool isDxt = (hdr.ddspf.flags & DDPF_FOURCC) != 0;
        bool isRgb = (hdr.ddspf.flags & DDPF_RGB) != 0;

        bgfx_texture_format_t fmt;
        u64 flags = worldWrap
            ? (BGFX_TEXTURE_NONE | BGFX_UI_TEX_MIN_ANISOTROPIC | BGFX_UI_TEX_MAG_ANISOTROPIC)
            : (BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP | BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_POINT);

            if (isDxt && fourcc == FOURCC('D', 'X', 'T', '1'))
            {
                fmt = BGFX_TEXTURE_FORMAT_BC1;
            }
            else if (isDxt && (fourcc == FOURCC('D', 'X', 'T', '3') || fourcc == FOURCC('D', 'X', 'T', '2')))
            {
                fmt = BGFX_TEXTURE_FORMAT_BC2;
            }
            else if (isDxt && (fourcc == FOURCC('D', 'X', 'T', '5') || fourcc == FOURCC('D', 'X', 'T', '4')))
            {
                fmt = BGFX_TEXTURE_FORMAT_BC3;
            }
            else if (!isDxt && isRgb && hdr.ddspf.rgbBitCount == 32)
            {
                fmt = BGFX_TEXTURE_FORMAT_BGRA8;
            }
            else if (!isDxt && !isRgb && (hdr.ddspf.flags & DDPF_ALPHA) && hdr.ddspf.rgbBitCount == 8)
            {
                // X-Ray font textures: 8-bit alpha-only DDS. Expand to RGBA8
                // (alpha = value) so shaders can sample .a directly.
                fmt = BGFX_TEXTURE_FORMAT_RGBA8;
                u32 mipBytes = w * h * 4;

                u8* rgba = (u8*)xr_malloc(mipBytes);
                const u8* src = data + 4 + sizeof(DdsHeader);
                for (u32 i = 0; i < w * h; i++)
                {
                    rgba[i * 4 + 0] = 255;
                    rgba[i * 4 + 1] = 255;
                    rgba[i * 4 + 2] = 255;
                    rgba[i * 4 + 3] = src[i];
                }
                const bgfx_memory_t* memA8 = bgfx_copy(rgba, mipBytes);
                tex = bgfx_create_texture_2d((u16)w, (u16)h, false, 1, fmt, flags, memA8, 0);
                xr_free(rgba);

                if (bgfxIsValid(tex))
                {
                    outW = w;
                    outH = h;
                }
                break;
            }
            else
                break;

            if (4 + sizeof(DdsHeader) + DdsMipLevelBytes(fmt, w, h) > size)
                break;

            u32 fullLevels = 1;
            for (u32 t = w > h ? w : h; t > 1; t >>= 1)
                fullLevels++;

            u32 fileLevels = 1;
            if ((hdr.flags & DDSD_MIPMAPCOUNT) && hdr.mipMapCount > 1)
                fileLevels = hdr.mipMapCount;
            if (fileLevels > fullLevels)
                fileLevels = fullLevels;

            u32 mipBytes = 0;
            {
                u32 lw = w, lh = h;
                for (u32 i = 0; i < fileLevels; i++)
                {
                    mipBytes += DdsMipLevelBytes(fmt, lw, lh);
                    lw = lw > 1 ? lw >> 1 : 1;
                    lh = lh > 1 ? lh >> 1 : 1;
                }
            }
            if (fileLevels > 1 && fileLevels != fullLevels)
            {
                fileLevels = 1;
                mipBytes = DdsMipLevelBytes(fmt, w, h);
            }

            if (4 + sizeof(DdsHeader) + mipBytes > size)
                break;

            const bgfx_memory_t* mem = bgfx_copy(data + 4 + sizeof(DdsHeader), mipBytes);
            tex = bgfx_create_texture_2d((u16)w, (u16)h, fileLevels > 1, 1, fmt, flags, mem, 0);

            if (bgfxIsValid(tex))
            {
                outW = w;
                outH = h;
            }
        } while (false);

        xr_free(data);
        return tex;
    }

bgfx_texture_handle_t LoadTGATexture(IReader* file, unsigned int& outW, unsigned int& outH, bool worldWrap)
{
    u32 size = file->elapsed();
    if (size < 18)
        return BGFX_INVALID_HANDLE;

    u8* data = (u8*)xr_malloc(size);
    file->r(data, (int)size);

    bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;
    do
    {
        u8 idLen = data[0];
        u8 imgType = data[2];
        u16 w = *(u16*)(data + 12);
        u16 h = *(u16*)(data + 14);
        u8 bpp = data[16];

        bool rle = (imgType == 10);
        bool uncompressed = (imgType == 2 || imgType == 3);
        if ((!uncompressed && !rle) || w == 0 || h == 0)
            break;
        if (bpp != 24 && bpp != 32 && !(imgType == 3 && bpp == 8))
            break;

        u32 bytesPerPixel = bpp / 8;
        u32 mipBytes = (u32)w * (u32)h * 4;
        u8* rgba = (u8*)xr_malloc(mipBytes);

        const u8* src = data + 18 + idLen;
        const u8* srcEnd = data + size;

        if (!rle)
        {
            u32 pixels = (u32)w * (u32)h;
            for (u32 i = 0; i < pixels; i++)
            {
                if (src + bytesPerPixel > srcEnd)
                    break;
                u8 b = src[0], g = src[1], r = bytesPerPixel > 2 ? src[2] : 0, a = bytesPerPixel > 3 ? src[3] : 255;
                rgba[i * 4 + 0] = r;
                rgba[i * 4 + 1] = g;
                rgba[i * 4 + 2] = b;
                rgba[i * 4 + 3] = a;
                src += bytesPerPixel;
            }
        }
        else
        {
            u32 total = (u32)w * (u32)h;
            u32 dst = 0;
            while (dst < total && src < srcEnd)
            {
                u8 packet = *src++;
                u32 count = (packet & 0x7F) + 1;
                if (packet & 0x80)
                {
                    if (src + bytesPerPixel > srcEnd)
                        break;
                    u8 b = src[0], g = src[1], r = bytesPerPixel > 2 ? src[2] : 0, a = bytesPerPixel > 3 ? src[3] : 255;
                    src += bytesPerPixel;
                    for (u32 i = 0; i < count && dst < total; i++, dst++)
                    {
                        rgba[dst * 4 + 0] = r;
                        rgba[dst * 4 + 1] = g;
                        rgba[dst * 4 + 2] = b;
                        rgba[dst * 4 + 3] = a;
                    }
                }
                else
                {
                    for (u32 i = 0; i < count && dst < total; i++, dst++)
                    {
                        if (src + bytesPerPixel > srcEnd)
                            break;
                        u8 b = src[0], g = src[1], r = bytesPerPixel > 2 ? src[2] : 0, a = bytesPerPixel > 3 ? src[3] : 255;
                        src += bytesPerPixel;
                        rgba[dst * 4 + 0] = r;
                        rgba[dst * 4 + 1] = g;
                        rgba[dst * 4 + 2] = b;
                        rgba[dst * 4 + 3] = a;
                    }
                }
            }
        }

        u64 flags = worldWrap
            ? (BGFX_TEXTURE_NONE | BGFX_UI_TEX_MIN_ANISOTROPIC | BGFX_UI_TEX_MAG_ANISOTROPIC)
            : (BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP | BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_POINT);
        const bgfx_memory_t* mem = bgfx_copy(rgba, mipBytes);
        tex = bgfx_create_texture_2d((u16)w, (u16)h, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, flags, mem, 0);
        xr_free(rgba);

            if (bgfxIsValid(tex))
            {
                outW = w;
                outH = h;
            }
        } while (false);

        xr_free(data);
        return tex;
    }

bool LoadUITexture(const char* texName, bgfx_texture_handle_t& outTex, unsigned int& outW, unsigned int& outH, bool worldWrap = false)
{
    outTex = BGFX_INVALID_HANDLE;
    outW = 0;
    outH = 0;

    IReader* file = NULL;
    char path[260];
    for (int attempt = 0; attempt < 2 && !file; attempt++)
    {
        const char* ext = (attempt == 0) ? ".dds" : ".tga";
        strconcat(sizeof(path), path, texName, ext);
        file = FS.r_open("$game_textures$", path);
    }

    if (!file)
    {
        // Level-scoped textures (e.g. terrain) live inside the level
        // folder ($level$), not under $game_textures$.
        strconcat(sizeof(path), path, texName, ".dds");
        file = FS.r_open("$level$", path);
    }

    if (!file)
    {
        // Some level textures (e.g. 'lod\level_lods') are stored at the
        // level root under their base name only.
        const char* base = strrchr(texName, '\\');
        base = base ? base + 1 : texName;
        strconcat(sizeof(path), path, base, ".dds");
        file = FS.r_open("$level$", path);
    }

    if (!file)
    {
        // .seq textures are handled by the caller (LoadUISeqTexture).
        LogError("[BGFX] UIShader: texture file not found: '%s'", texName);
        return false;
    }

    outTex = LoadDDSTexture(file, outW, outH, worldWrap);
    if (!bgfxIsValid(outTex))
    {
        file->seek(0);
        outTex = LoadTGATexture(file, outW, outH, worldWrap);
    }

    FS.r_close(file);

    if (!bgfxIsValid(outTex))
    {
        LogError("[BGFX] UIShader: failed to load texture '%s'", texName);
        return false;
    }
    return true;
}

    // .seq format (same as CTexture::Load): optional "cycled" flag line,
    // then fps, then one texture name per line for every frame.
    bool LoadUISeqTexture(const char* texName, UITextureCacheItem& item)
    {
        char path[260];
        strconcat(sizeof(path), path, texName, ".seq");
        IReader* seq = FS.r_open("$game_textures$", path);
        if (!seq)
            return false;

        char line[260];
        seq->r_string(line, sizeof(line));
        _Trim(line);
        item.seqCycles = false;
        if (0 == stricmp(line, "cycled"))
        {
            item.seqCycles = true;
            seq->r_string(line, sizeof(line));
            _Trim(line);
        }
        int fps = atoi(line);
        if (fps < 1)
            fps = 1;
        item.seqMSPF = u32(1000 / fps);

        item.seqFrames.clear();
        item.texture = BGFX_INVALID_HANDLE;
        item.width = 0;
        item.height = 0;

        while (!seq->eof())
        {
            seq->r_string(line, sizeof(line));
            _Trim(line);
            if (!line[0])
                continue;

            bgfx_texture_handle_t frameTex;
            unsigned int w = 0, h = 0;
            if (LoadUITexture(line, frameTex, w, h))
            {
                item.seqFrames.push_back(frameTex);
                if (item.seqFrames.size() == 1)
                {
                    item.texture = frameTex;
                    item.width = w;
                    item.height = h;
                }
            }
        }
        FS.r_close(seq);

        if (item.seqFrames.empty())
        {
            LogError("[BGFX] UIShader: .seq has no frames: '%s'", texName);
            return false;
        }
        return true;
    }
}

void bgfxUIShader::Copy(IUIShader &_in)
{
    *this = *(bgfxUIShader*)&_in;
}

namespace
{
    // Decoders for .ogm UI statics (menu background videos), shared by
    // all shaders referencing the same video.
    std::map<std::string, BgfxVideoDecoder*> g_MovieShaderDecoders;

    BgfxVideoDecoder* GetMovieDecoder(const char* texName)
    {
        auto it = g_MovieShaderDecoders.find(texName);
        if (it != g_MovieShaderDecoders.end())
            return it->second;

        BgfxVideoDecoder* decoder = xr_new<BgfxVideoDecoder>();
        string_path path;
        strconcat(sizeof(path), path, texName, ".ogm");
        if (!decoder->Open(path))
        {
            xr_delete(decoder);
            return nullptr;
        }
        g_MovieShaderDecoders[texName] = decoder;
        return decoder;
    }
}

void bgfxUIShader::create(LPCSTR sh, LPCSTR tex, bool no_cache)
{
    m_bInited = true;
    m_bMovie = false;
    m_movieDecoder = nullptr;
    m_movieStartTick = 0;
    m_seqFrames.clear();
    m_seqMSPF = 0;
    m_seqCycles = false;

    if (!tex || !tex[0])
    {
        m_texture = BGFX_INVALID_HANDLE;
        m_width = 0;
        m_height = 0;
        m_seqFrames.clear();
        m_seqMSPF = 0;
        m_seqCycles = false;
        m_texName[0] = 0;
        return;
    }

    strncpy_s(m_texName, tex, sizeof(m_texName) - 1);

    // Movie UI statics reference .ogm video files.
    size_t texLen = strlen(m_texName);
    bool isMovieTex = (texLen > 4 && 0 == _stricmp(m_texName + texLen - 4, ".ogm"));
    bool isMovieShader = (sh && 0 == _stricmp(sh, "hud\\movie"));
    if (isMovieTex || isMovieShader)
    {
        // Sequence-driven videos (intro/outro) are decoded and drawn by
        // the IRenderTexture path — the UI must skip these quads.
        if (0 == _strnicmp(m_texName, "intro\\", 6) || 0 == _strnicmp(m_texName, "outro\\", 6))
        {
            m_bMovie = true;
            m_texture = BGFX_INVALID_HANDLE;
            m_width = 0;
            m_height = 0;
            return;
        }

        // Animated UI static (menu background video): the shader plays the
        // video itself, like CTexture::apply_theora in the D3D9 renderer.
        m_movieDecoder = GetMovieDecoder(m_texName);
        if (m_movieDecoder)
        {
            m_movieDecoder->Play();
            m_movieStartTick = GetTickCount();
            m_texture = m_movieDecoder->GetTexture();
            m_width = m_movieDecoder->GetWidth();
            m_height = m_movieDecoder->GetHeight();
            LogInfo("[BGFX] UIShader: movie static '%s' %ux%u", m_texName, m_width, m_height);
        }
        else
        {
            m_bMovie = true;
            m_texture = BGFX_INVALID_HANDLE;
            m_width = 0;
            m_height = 0;
        }
        return;
    }

    auto it = g_UITextureCache.find(m_texName);
    if (it != g_UITextureCache.end())
    {
        m_texture = it->second.texture;
        m_width = it->second.width;
        m_height = it->second.height;
        m_seqFrames = it->second.seqFrames;
        m_seqMSPF = it->second.seqMSPF;
        m_seqCycles = it->second.seqCycles;
        return;
    }

    UITextureCacheItem item;
    item.texture = BGFX_INVALID_HANDLE;
    item.width = 0;
    item.height = 0;
    item.seqMSPF = 0;
    item.seqCycles = false;

    if (!LoadUITexture(m_texName, item.texture, item.width, item.height))
        LoadUISeqTexture(m_texName, item);

    if (bgfxIsValid(item.texture) || !item.seqFrames.empty())
    {
        g_UITextureCache[m_texName] = item;
        m_texture = item.texture;
        m_width = item.width;
        m_height = item.height;
        m_seqFrames = item.seqFrames;
        m_seqMSPF = item.seqMSPF;
        m_seqCycles = item.seqCycles;
        LogInfo("[BGFX] UIShader: texture '%s' %ux%u frames=%u", m_texName, m_width, m_height, (u32)m_seqFrames.size());
    }
    else
    {
        m_texture = BGFX_INVALID_HANDLE;
        m_width = 0;
        m_height = 0;
    }
}

bgfx_texture_handle_t bgfxUIShader::GetTexture() const
{
    if (m_movieDecoder)
    {
        // Advance the video to the current time (loop by restarting when
        // the data ends), like CTexture::apply_theora.
        if (m_movieDecoder->IsEnded())
        {
            m_movieDecoder->Restart();
            m_movieStartTick = GetTickCount();
        }
        if (m_movieDecoder->IsPlaying())
            m_movieDecoder->Sync(GetTickCount() - m_movieStartTick);
        return m_movieDecoder->GetTexture();
    }

    if (m_seqFrames.empty())
        return m_texture;

    // Frame selection mirrors CTexture::apply_seq.
    u32 frame = GetTickCount() / m_seqMSPF;
    u32 count = (u32)m_seqFrames.size();
    if (m_seqCycles)
    {
        u32 frame_id = frame % (count * 2);
        if (frame_id >= count)
            frame_id = (count - 1) - (frame_id % count);
        return m_seqFrames[frame_id];
    }
    return m_seqFrames[frame % count];
}

bool bgfxUIShader::inited()
{
    return m_bInited;
}

bool bgfxLoadUITexture(LPCSTR texName, bgfx_texture_handle_t& outTex, unsigned int& outW, unsigned int& outH)
{
    return LoadUITexture(texName, outTex, outW, outH, false);
}

bool bgfxLoadWorldTexture(LPCSTR texName, bgfx_texture_handle_t& outTex, unsigned int& outW, unsigned int& outH)
{
    return LoadUITexture(texName, outTex, outW, outH, true);
}
