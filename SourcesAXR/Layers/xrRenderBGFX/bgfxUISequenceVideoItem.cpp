#include "stdafx.h"
#include "bgfxUISequenceVideoItem.h"
#include "bgfxUIProgram.h"
#include "bgfxRenderInterface.h"

bgfxUISequenceVideoItem::bgfxUISequenceVideoItem()
    : m_textureCaptured(false)
{
}

bgfxUISequenceVideoItem::~bgfxUISequenceVideoItem()
{
    m_decoder.Close();
}

void bgfxUISequenceVideoItem::Copy(IUISequenceVideoItem &_in)
{
    // not supported
}

bool bgfxUISequenceVideoItem::Open(const char* _videoPath)
{
    if (!_videoPath || !_videoPath[0])
        return false;

    LogInfo("[BGFX] VideoItem::Open(%s)", _videoPath);
    return m_decoder.Open(_videoPath);
}

bool bgfxUISequenceVideoItem::HasTexture()
{
    return m_decoder.HasTexture();
}

void bgfxUISequenceVideoItem::CaptureTexture()
{
    // Called once from OnRender when texture is not yet available.
    // Decode the first frame and render it.
    if (m_decoder.HasTexture())
    {
        m_decoder.DecodeFrame();
        RenderVideoFrame();
        m_textureCaptured = true;
    }
}

void bgfxUISequenceVideoItem::ResetTexture()
{
    m_textureCaptured = false;
}

BOOL bgfxUISequenceVideoItem::video_IsPlaying()
{
    return m_decoder.IsPlaying() ? TRUE : FALSE;
}

void bgfxUISequenceVideoItem::video_Sync(u32 _time)
{
    // Called every frame while playing. Decode frames up to the given time
    // and render the latest one.
    if (!m_decoder.HasTexture())
        return;

    m_decoder.Sync(_time);
    RenderVideoFrame();
}

void bgfxUISequenceVideoItem::video_Play(BOOL looped, u32 _time)
{
    m_decoder.Play();
    if (_time != 0xFFFFFFFF)
        m_decoder.Sync(_time);
}

void bgfxUISequenceVideoItem::video_Stop()
{
    m_decoder.Stop();
}

void bgfxUISequenceVideoItem::RenderVideoFrame()
{
    bgfx_texture_handle_t tex = m_decoder.GetTexture();
    if (!bgfxIsValid(tex))
        return;

    bgfx_program_handle_t prog = bgfxUITexturedProgramGet();
    if (!bgfxUIProgramValid(prog))
        return;

    unsigned int w = m_decoder.GetWidth();
    unsigned int h = m_decoder.GetHeight();
    if (w == 0 || h == 0)
        return;

    // Texture may be padded to a multiple of 16 — trim UVs to the real size
    // so sampling never reaches the padding rows/cols.
    unsigned int texW = (w + 15) & ~15u;
    unsigned int texH = (h + 15) & ~15u;
    float uMax = (float)w / (float)texW;
    float vMax = (float)h / (float)texH;

    unsigned int rtW = g_bgfxRenderTarget.m_width;
    unsigned int rtH = g_bgfxRenderTarget.m_height;
    if (rtW == 0) rtW = 1920;
    if (rtH == 0) rtH = 1080;

    // Fit the video into the render target preserving its aspect ratio
    // (letterbox — the rest stays black from the clear color), slightly
    // smaller than the full screen.
    const float videoScale = 0.9f;
    float videoAspect = (float)w / (float)h;
    float rtAspect = (float)rtW / (float)rtH;

    float xHalf = 1.0f * videoScale, yHalf = 1.0f * videoScale;
    if (videoAspect > rtAspect)
    {
        // Video is wider than the screen: fit width, letterbox top/bottom
        yHalf = (rtAspect / videoAspect) * videoScale;
    }
    else
    {
        // Video is taller than the screen: fit height, pillarbox left/right
        xHalf = (videoAspect / rtAspect) * videoScale;
    }

    // Full-screen quad in clip space (y-flipped for bgfx/D3D11)
    // Positions: TL, TR, BR, BL
    struct V { float x, y, z; unsigned char r, g, b, a; float u, v; };
    V verts[4];

    // TL
    verts[0] = { -xHalf,  yHalf, 0.0f, 255, 255, 255, 255, 0.0f,  0.0f  };
    // TR
    verts[1] = {  xHalf,  yHalf, 0.0f, 255, 255, 255, 255, uMax, 0.0f  };
    // BR
    verts[2] = {  xHalf, -yHalf, 0.0f, 255, 255, 255, 255, uMax, vMax };
    // BL
    verts[3] = { -xHalf, -yHalf, 0.0f, 255, 255, 255, 255, 0.0f,  vMax };

    bgfx_transient_vertex_buffer_t tvb;
    bgfx_alloc_transient_vertex_buffer(&tvb, 4, &g_uiVertexLayout);
    if (!tvb.data)
        return;

    memcpy(tvb.data, verts, sizeof(verts));

    uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    bgfx_transient_index_buffer_t tib;
    bgfx_alloc_transient_index_buffer(&tib, 6, false);
    if (!tib.data)
        return;
    memcpy(tib.data, indices, sizeof(indices));

    // Bind texture to sampler
    bgfx_uniform_handle_t sampler = bgfxUITextureSamplerGet();
    bgfx_set_texture(0, sampler, tex, UINT32_MAX);

    bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
    bgfx_set_transient_vertex_buffer(0, &tvb, 0, 4);
    bgfx_set_transient_index_buffer(&tib, 0, 6);
    // View 1 draws after all view 0 (UI) submits so the video is on top.
    // Any 2D UI submitted later in this frame (the fullscreen menu/intro
    // backdrop goes through bgfxUIShader after Render->Render()) must fall
    // back to the world view to keep the video visible.
    bgfxUISubmitView() = 0;
    bgfx_submit(1, prog, 0, BGFX_DISCARD_ALL);
}
