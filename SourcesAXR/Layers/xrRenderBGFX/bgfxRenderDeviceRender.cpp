// BGFX Renderer - uses bgfx C API for C++17 compatibility
#include "stdafx.h"
#include "bgfxRenderDeviceRender.h"
#include "bgfxRenderInterface.h"
#include "bgfxImGuiRender.h"
#include "port\bgfxHDR.h"
#include "bgfx_capi.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

// ImGui initialization callback (called from bgfx thread)
static bool g_bImGuiInitialized = false;

// ---- bgfx C callback interface (screenshot support) ----
// Layout must match bgfx's generated bgfx_callback_vtbl_t / interface:
// vtbl pointer first, impl second; every function receives the interface
// pointer as its first argument.
namespace
{
    struct XrCallbackVtbl
    {
        void (*fatal)(void* impl, const char* filePath, uint16_t line, int code, const char* str);
        void (*traceVargs)(void* impl, const char* filePath, uint16_t line, const char* format, va_list argList);
        void (*profilerBegin)(void* impl, const char* name, uint32_t abgr, const char* filePath, uint16_t line);
        void (*profilerBeginLiteral)(void* impl, const char* name, uint32_t abgr, const char* filePath, uint16_t line);
        void (*profilerEnd)(void* impl);
        uint32_t (*cacheReadSize)(void* impl, uint64_t id);
        bool (*cacheRead)(void* impl, uint64_t id, void* data, uint32_t size);
        void (*cacheWrite)(void* impl, uint64_t id, const void* data, uint32_t size);
        void (*screenShot)(void* impl, const char* filePath, uint32_t width, uint32_t height, uint32_t pitch, int format, const void* data, uint32_t size, bool yflip);
        void (*captureBegin)(void* impl, uint32_t width, uint32_t height, uint32_t pitch, int format, bool yflip);
        void (*captureEnd)(void* impl);
        void (*captureFrame)(void* impl, const void* data, uint32_t size);
    };

    struct XrCallbackInterface
    {
        const XrCallbackVtbl* vtbl;
        void* impl;
    };

    void XrCbFatal(void*, const char*, uint16_t, int, const char* str)
    {
        LogError("[BGFX] bgfx fatal: %s", str ? str : "?");
    }
    void XrCbTraceVargs(void*, const char*, uint16_t, const char*, va_list) {}
    void XrCbProfilerBegin(void*, const char*, uint32_t, const char*, uint16_t) {}
    void XrCbProfilerBeginLiteral(void*, const char*, uint32_t, const char*, uint16_t) {}
    void XrCbProfilerEnd(void*) {}
    uint32_t XrCbCacheReadSize(void*, uint64_t) { return 0; }
    bool XrCbCacheRead(void*, uint64_t, void*, uint32_t) { return false; }
    void XrCbCacheWrite(void*, uint64_t, const void*, uint32_t) {}

    void XrCbScreenShot(void*, const char* filePath, uint32_t width, uint32_t height, uint32_t pitch, int, const void* data, uint32_t size, bool yflip)
    {
        LogInfo("[BGFX] Screenshot cb '%s': %ux%u pitch=%u size=%u yflip=%d",
            filePath ? filePath : "?", width, height, pitch, size, yflip ? 1 : 0);
        if (!filePath || !data || width == 0 || height == 0)
            return;

        IWriter* fs = FS.w_open("$screenshots$", filePath);
        if (!fs)
            return;

        // TGA: uncompressed 32-bit BGRA
        u8 hdr[18];
        memset(hdr, 0, sizeof(hdr));
        hdr[2]  = 2;
        hdr[12] = (u8)(width & 0xFF);
        hdr[13] = (u8)((width >> 8) & 0xFF);
        hdr[14] = (u8)(height & 0xFF);
        hdr[15] = (u8)((height >> 8) & 0xFF);
        hdr[16] = 32;
        hdr[17] = yflip ? 0x00 : 0x20; // bottom-up origin when yflip
        fs->w(hdr, 18);

        if (pitch == width * 4)
            fs->w(data, width * height * 4);
        else
            for (u32 y = 0; y < height; y++)
                fs->w((const u8*)data + y * pitch, width * 4);

        FS.w_close(fs);
        LogInfo("[BGFX] Screenshot saved: $screenshots$\\%s", filePath);
    }
    void XrCbCaptureBegin(void*, uint32_t, uint32_t, uint32_t, int, bool) {}
    void XrCbCaptureEnd(void*) {}
    void XrCbCaptureFrame(void*, const void*, uint32_t) {}

    const XrCallbackVtbl s_xrCallbackVtbl = {
        &XrCbFatal,
        &XrCbTraceVargs,
        &XrCbProfilerBegin,
        &XrCbProfilerBeginLiteral,
        &XrCbProfilerEnd,
        &XrCbCacheReadSize,
        &XrCbCacheRead,
        &XrCbCacheWrite,
        &XrCbScreenShot,
        &XrCbCaptureBegin,
        &XrCbCaptureEnd,
        &XrCbCaptureFrame,
    };
    XrCallbackInterface s_xrCallback = { &s_xrCallbackVtbl, nullptr };
}

// View map: 0 = HDR scene, 1 = intro video, 2 = combine, 3 = HUD, 4 = game UI, 5 = ImGui, 6 = scene FX.

bgfxRenderDeviceRender::bgfxRenderDeviceRender()
    : m_bInitialized(false)
    , m_hWnd(nullptr)
    , m_width(0)
    , m_height(0)
    , m_fGamma(1.0f)
    , m_fBrightness(1.0f)
    , m_fContrast(1.0f)
    , m_bForceGPU_REF(FALSE)
    , m_deviceState(dsOK)
{
}

bgfxRenderDeviceRender::~bgfxRenderDeviceRender()
{
    ShutdownBGFX();
}

void bgfxRenderDeviceRender::Copy(IRenderDeviceRender &_in)
{
    *this = *(bgfxRenderDeviceRender*)&_in;
}

void bgfxRenderDeviceRender::setGamma(float fGamma)
{
    m_fGamma = fGamma;
}

void bgfxRenderDeviceRender::setBrightness(float fGamma)
{
    m_fBrightness = fGamma;
}

void bgfxRenderDeviceRender::setContrast(float fGamma)
{
    m_fContrast = fGamma;
}

void bgfxRenderDeviceRender::updateGamma()
{
}

void bgfxRenderDeviceRender::OnDeviceDestroy(BOOL bKeepTextures)
{
    if (m_bInitialized)
        bgfxHDR::DestroyHDRTarget();
}

void bgfxRenderDeviceRender::ValidateHW()
{
}

void bgfxRenderDeviceRender::DestroyHW()
{
    ShutdownBGFX();
}

void bgfxRenderDeviceRender::Reset(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2)
{
    if (m_bInitialized)
    {
        bgfxHDR::DestroyHDRTarget();
        bgfx_reset(dwWidth, dwHeight, BGFX_RESET_NONE, BGFX_TEXTURE_FORMAT_COUNT);
    }
    m_width = dwWidth;
    m_height = dwHeight;

    RECT rc;
    if (GetClientRect(m_hWnd, &rc) && rc.right > 0 && rc.bottom > 0)
    {
        m_width = rc.right;
        m_height = rc.bottom;
    }

    g_bgfxRenderTarget.m_width = m_width;
    g_bgfxRenderTarget.m_height = m_height;

    if (m_bInitialized)
        bgfxHDR::CreateHDRTarget((uint16_t)m_width, (uint16_t)m_height);

    // Write the actual size back to the engine (out-params)
    dwWidth = m_width;
    dwHeight = m_height;
    fWidth_2 = float(dwWidth / 2);
    fHeight_2 = float(dwHeight / 2);
}

void bgfxRenderDeviceRender::SetupStates()
{
}

void bgfxRenderDeviceRender::OnDeviceCreate(LPCSTR shName)
{
}

void bgfxRenderDeviceRender::Create(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2, bool)
{
    LogInfo("[BGFX] Create(%p, %u, %u)", (void*)hWnd, dwWidth, dwHeight);
    m_hWnd = hWnd;
    m_width = dwWidth;
    m_height = dwHeight;
    fWidth_2 = float(dwWidth / 2);
    fHeight_2 = float(dwHeight / 2);

    // Get actual window size (Create may be called before the window has its final size)
    RECT rc;
    if (GetClientRect(hWnd, &rc) && rc.right > 0 && rc.bottom > 0)
    {
        m_width = rc.right;
        m_height = rc.bottom;
    }

    g_bgfxRenderTarget.m_width = m_width;
    g_bgfxRenderTarget.m_height = m_height;

    // Write the actual size back to the engine (out-params): the engine's
    // Device.dwWidth/dwHeight drive UI scaling and font variant selection.
    dwWidth = m_width;
    dwHeight = m_height;
    fWidth_2 = float(dwWidth / 2);
    fHeight_2 = float(dwHeight / 2);

    // Use the actual window size (m_width/m_height), not the incoming
    // dwWidth/dwHeight which are still 0 at Create time before the window
    // has its final size. Passing 0x0 makes bgfx create a 0x0 swap chain
    // and nothing visible ever renders.
    InitBGFX(hWnd, m_width, m_height);

    // Initialize ImGui if not already done
    if (!g_bImGuiInitialized)
    {
        LogInfo("[BGFX] Initializing ImGui");
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hWnd);

        g_bImGuiInitialized = true;
        LogInfo("[BGFX] ImGui initialized");
    }

    bgfxImguiInit();

    LogInfo("[BGFX] Create complete");
}

void bgfxRenderDeviceRender::SetupGPU(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF)
{
    m_bForceGPU_REF = bForceGPU_REF;
}

void bgfxRenderDeviceRender::overdrawBegin()
{
}

void bgfxRenderDeviceRender::overdrawEnd()
{
}

void bgfxRenderDeviceRender::DeferredLoad(BOOL E)
{
}

void bgfxRenderDeviceRender::ResourcesDeferredUpload()
{
}

void bgfxRenderDeviceRender::ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps)
{
    m_base = 0;
    c_base = 0;
    m_lmaps = 0;
    c_lmaps = 0;
}

void bgfxRenderDeviceRender::ResourcesDestroyNecessaryTextures()
{
}

void bgfxRenderDeviceRender::ResourcesStoreNecessaryTextures()
{
}

void bgfxRenderDeviceRender::ResourcesDumpMemoryUsage()
{
}

void bgfxRenderDeviceRender::RenderPrefetchUITextures()
{
}

bool bgfxRenderDeviceRender::HWSupportsShaderYUV2RGB()
{
    return false;
}

IRenderDeviceRender::DeviceState bgfxRenderDeviceRender::GetDeviceState()
{
    return m_deviceState;
}

BOOL bgfxRenderDeviceRender::GetForceGPU_REF()
{
    return m_bForceGPU_REF;
}

u32 bgfxRenderDeviceRender::GetCacheStatPolys()
{
    return 0;
}

void bgfxRenderDeviceRender::Begin()
{
    if (!m_bInitialized)
        return;

    const bool hdrReady = bgfxHDR::RecreateOnResize((uint16_t)m_width, (uint16_t)m_height) && bgfxHDR::BindScene();
    if (!hdrReady)
    {
        LogError("[BGFX] HDR scene target unavailable; using direct backbuffer");
        bgfx_set_view_frame_buffer(bgfxHDR::kSceneView, BGFX_INVALID_HANDLE);
        bgfx_set_view_frame_buffer(bgfxHDR::kSceneFxView, BGFX_INVALID_HANDLE);
        bgfx_set_view_rect(bgfxHDR::kSceneView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
        bgfx_set_view_clear(bgfxHDR::kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
        bgfx_set_view_mode(bgfxHDR::kSceneView, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_touch(bgfxHDR::kSceneView);
        bgfx_set_view_rect(bgfxHDR::kSceneFxView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
        bgfx_set_view_mode(bgfxHDR::kSceneFxView, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_touch(bgfxHDR::kSceneFxView);
    }

    // Intro video / second-viewport playback. The view always exists in the
    // order; bgfxUISequenceVideoItem::Render submits into it only while an intro
    // clip is playing, and an empty view draws nothing.
    bgfx_set_view_frame_buffer(bgfxHDR::kIntroView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kIntroView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kIntroView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kIntroView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kIntroView);

    bgfx_set_view_frame_buffer(bgfxHDR::kCombineView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kCombineView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kCombineView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kCombineView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kCombineView);

    // 2D UI: the default target of bgfxUISubmitView(). The video item switches that
    // to the intro view while a clip plays (bgfxUISequenceVideoItem.cpp), which is
    // why both views are bound here rather than at their submit sites.
    bgfx_set_view_frame_buffer(bgfxHDR::kHudView, BGFX_INVALID_HANDLE);
    bgfx_set_view_frame_buffer(bgfxHDR::kUiView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kUiView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kUiView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kUiView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kUiView);

    // Screen-space ambient occlusion. The view always exists in the order so the
    // pass order stays stable; bgfxHDR::SSAOPass rebinds it to the half-res
    // occlusion target and submits only when that target, the jitter texture and
    // its program are available, otherwise the resolve samples the neutral 1.0.
    bgfx_set_view_frame_buffer(bgfxHDR::kSsaoView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kSsaoView, 0, 0, (uint16_t)(m_width / 2), (uint16_t)(m_height / 2));
    bgfx_set_view_clear(bgfxHDR::kSsaoView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kSsaoView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kSsaoView);

    // Lighting resolve. The view always exists in the order so the pass order
    // stays stable; bgfxHDR::ResolvePass rebinds it to the lit target and submits
    // only when that target and its program are available, otherwise the view
    // draws nothing and the post chain falls back to the unlit attachment 0.
    bgfx_set_view_frame_buffer(bgfxHDR::kResolveView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kResolveView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kResolveView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kResolveView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kResolveView);

    // Split-HDR high channel. The view always exists in the order so the pass order
    // stays stable; bgfxHDR::HighPass rebinds it to the high target and submits only
    // when that target and its program are available, otherwise the bloom bright pass
    // skips instead of reading an un-encoded image.
    bgfx_set_view_frame_buffer(bgfxHDR::kHighView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kHighView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kHighView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kHighView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kHighView);

    // Stage-1 G-buffer inspector. The view always exists in the order so the
    // pass order stays stable; bgfxHDR::GbufDebugPass submits into it only when
    // XRGBUF_DEBUG is set, and an empty view draws nothing.
    bgfx_set_view_frame_buffer(bgfxHDR::kGbufDebugView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kGbufDebugView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kGbufDebugView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kGbufDebugView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kGbufDebugView);

    // Rain (AXR render_rain, r4_R_render.cpp:505). It draws rain and
    // thunderbolts, and the reference renders it forward with blending off
    // (dx10RainBlender.cpp:13) straight into rt_Generic_0
    // (r4_rendertarget_draw_rain.cpp), i.e. it is NOT deferred G-buffer geometry.
    // The framebuffer here is still the scene one: moving the pass to the
    // reference's own forward stage is the rain port's job, this renumbering
    // only gave it a view of its own. It sits right after the wallmarks, where
    // r4_R_render.cpp:505 calls it, and before the lighting accumulation.
    bgfx_set_view_frame_buffer(bgfxHDR::kRainView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kRainView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kRainView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kRainView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kRainView);

    // Wallmarks (AXR CRenderTarget::phase_wallmarks,
    // r4_rendertarget_phase_combine.cpp:791-804). The view always exists in the
    // order; bgfxWallMarks::Render rebinds it to the albedo+depth target and
    // submits only when that target is available, otherwise an empty view draws
    // nothing. It sits after the scene FX view - the last G-buffer writer - and
    // before SSAO, matching r4_R_render.cpp:464 where the phase runs after the
    // level/lods/Detail passes and before the lighting accumulations.
    bgfx_set_view_frame_buffer(bgfxHDR::kWallmarkView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kWallmarkView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kWallmarkView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kWallmarkView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kWallmarkView);

    // SMAA (AXR blender_smaa + rendertarget_phase_smaa.cpp): edge -> weights ->
    // resolve. Views always exist in the order; bgfxHDR::SMAAPass submits into
    // them only when its targets/programs/textures are ready, otherwise the
    // combine presents to the backbuffer directly and empty views draw nothing.
    bgfx_set_view_frame_buffer(bgfxHDR::kSmaaEdgeView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kSmaaEdgeView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kSmaaEdgeView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kSmaaEdgeView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kSmaaEdgeView);
    bgfx_set_view_frame_buffer(bgfxHDR::kSmaaBlendView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kSmaaBlendView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kSmaaBlendView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kSmaaBlendView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kSmaaBlendView);
    bgfx_set_view_frame_buffer(bgfxHDR::kSmaaResolveView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kSmaaResolveView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kSmaaResolveView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kSmaaResolveView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kSmaaResolveView);

    // Forward rendering (AXR r4_rendertarget_phase_combine.cpp:374-388,
    // RImplementation.render_forward() at :386): the geometry that cannot be
    // deferred, i.e. the mixed and additive particles. The view always exists in
    // the order so the pass order stays stable, and no pass submits into it yet,
    // so it draws nothing.
    bgfx_set_view_frame_buffer(bgfxHDR::kForwardView, BGFX_INVALID_HANDLE);
    bgfx_set_view_rect(bgfxHDR::kForwardView, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(bgfxHDR::kForwardView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kForwardView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kForwardView);

    // Frame order, 1:1 with AXR R4:
    //
    //   casters -> scene (level / lods / Details) -> world dynamics ->
    //   wallmarks -> rain -> SSAO -> lighting resolve -> high -> sky + clouds ->
    //   bloom bright pass -> luminance -> gauss H/V -> combine_1 -> forward ->
    //   SMAA -> G-buffer inspector -> UI.
    //
    // References: r4_R_render.cpp:392-397 (level, lods, Details), :633
    // (render_main / r_dsgraph_render_graph(1)), :538/:546
    // (r_dsgraph_render_emissive), :472 (phase_wallmarks), :505 (render_rain),
    // :509-521 (sun cascades + accum_direct_blend), :526-547 (phase_accumulator
    // + emissive), :553-557 (phase_accumulator + render_lights), :574
    // (phase_combine). Inside r4_rendertarget_accum_direct.cpp:685 the accum
    // family writes rt_Accumulator, a target of its own, which is why
    // r4_rendertarget_phase_combine.cpp:143-145 can clear rt_Generic_0/1 and
    // draw the sky and the clouds over the lit result (:166, :170, CULL_NONE at
    // :153, no stencil at :154) and still have combine_1 blend the sky over the
    // light by the stencil >= 1 mask (:178). In this port the accumulator IS the
    // lit target, so the resolve has to precede the sky and the high channel has
    // to precede it too - sky2.ps:60 writes the high channel itself.
    //
    // The rest of the chain: the bright pass (r4_rendertarget_phase_bloom.cpp:74-127)
    // reads the high channel (blender_bloom_build.cpp:18) and has to run after
    // both of its writers; the luminance chain (:131 -> r4_rendertarget_phase_luminance.cpp:19-143)
    // measures the bright-pass output (blender_luminance.cpp:18) before the two
    // gaussian passes overwrite rt_Bloom_1 (:237, :317); the combine is the
    // consumer of rt_Bloom_1 (combine_bloom); forward rendering follows
    // combine_1 (r4_rendertarget_phase_combine.cpp:374-388, render_forward :386);
    // SMAA (:470-473) resolves into the backbuffer and the inspector runs last
    // because it repaints the finished frame.
    //
    // The array is not decoration: bgfx_set_viewOrder copies it verbatim into
    // m_viewRemap[0..count-1] (bgfx_p.h:7197 setViewOrder), i.e. entry k *is* the
    // internal slot of the raw view id k, and every id in [0, count) has to appear
    // exactly once. A missing id leaves m_viewOrder[slot] pointing at the other
    // view that shares the slot, and the two then share a sort bucket - the second
    // one renders with the first one's framebuffer and rect, so the two passes
    // land in one target. An id listed twice displaces another id, and that view
    // is then never rendered at all.
    //
    // The ids in port/bgfxHDR.h are numbered in this reference order, so the table
    // below is the identity and ascending id is the frame order. Two deliberate
    // exceptions:
    //   * kShadowView runs in front of the scene view even though its id is above
    //     0, because the caster pass has to run before the geometry so the map
    //     exists by the time the resolve samples it (r2_R_sun.cpp:722-741 is drawn
    //     before r4_R_render.cpp:392-397).
    //   * kSkyView runs inside the G-buffer block, before SSAO and the resolve,
    //     which is NOT its id 8 slot. The reference draws the sky into rt_Generic_0
    //     / rt_Generic_1 (r4_rendertarget_phase_combine.cpp:143-145, :166, :170)
    //     and blends it in combine_1, because there the accumulator is a target of
    //     its own (r4_rendertarget_accum_direct.cpp:685 rt_Accumulator). This port
    //     has the opposite shape: the resolve writes the lit image in place, so a
    //     sky drawn after it would be overwritten. Until the sky moves to the low
    //     and high targets it therefore has to stay a G-buffer writer, and its id
    //     8 slot becomes correct as soon as that move lands. TODO: move the submit
    //     to the low/high targets, then restore the ascending order.
    const bgfx_view_id_t order[] = { bgfxHDR::kShadowView, bgfxHDR::kSceneView, bgfxHDR::kSceneFxView,
        bgfxHDR::kWallmarkView, bgfxHDR::kRainView, bgfxHDR::kSkyView,
        bgfxHDR::kSsaoView, bgfxHDR::kResolveView,
        bgfxHDR::kHighView, bgfxHDR::kBloomBuildView,
        bgfxHDR::kLuminance64View, bgfxHDR::kLuminance8View, bgfxHDR::kLuminance1View,
        bgfxHDR::kBloomBlurHView, bgfxHDR::kBloomBlurVView,
        bgfxHDR::kCombineView, bgfxHDR::kForwardView,
        bgfxHDR::kSmaaEdgeView, bgfxHDR::kSmaaBlendView,
        bgfxHDR::kSmaaResolveView, bgfxHDR::kGbufDebugView,
        bgfxHDR::kIntroView, bgfxHDR::kHudView, bgfxHDR::kUiView, bgfxHDR::kImguiView };
    bgfx_set_view_order(bgfxHDR::kSceneView, sizeof(order) / sizeof(order[0]), order);
}

void bgfxRenderDeviceRender::Clear()
{
    bgfx_set_view_clear(bgfxHDR::kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    bgfx_touch(bgfxHDR::kSceneView);
}

void bgfxRenderDeviceRender::End()
{
    // Reset the 2D UI view for the next frame: by default it renders after the
    // HUD view; the video item switches it to the intro view per frame.
    bgfxUISubmitView() = bgfxHDR::kUiView;
}

void bgfxRenderDeviceRender::ClearTarget()
{
}

void bgfxRenderDeviceRender::SetCacheXform(Fmatrix &mView, Fmatrix &mProject)
{
    bgfx_set_view_transform(bgfxHDR::kSceneView, mView.m, mProject.m);
    bgfx_set_view_transform(bgfxHDR::kSceneFxView, mView.m, mProject.m);
    // The wallmark quads are world-space (WallmarksEngine.cpp:348 sets the world
    // xform to identity and the projection to Device.mProject), so the phase view
    // needs the same transform the scene view has.
    bgfx_set_view_transform(bgfxHDR::kWallmarkView, mView.m, mProject.m);
}

void bgfxRenderDeviceRender::OnAssetsChanged()
{
}

IResourceManager* bgfxRenderDeviceRender::GetResourceManager() const
{
    return nullptr;
}

void bgfxRenderDeviceRender::PresentFrame()
{
    bgfxImguiRenderFrame();
    bgfx_frame(BGFX_FRAME_NONE);
}

bool bgfxRenderDeviceRender::InitBGFX(HWND hWnd, u32 width, u32 height)
{
    if (m_bInitialized)
        return true;

    bgfx_init_t init;
    bgfx_init_ctor(&init);
    // All four backends are compiled (BGFX_CONFIG_RENDERER_DIRECT3D11/DIRECT3D12/VULKAN/OPENGL).
    // Shaders are precompiled into per-backend blobs (dxbc/dxil/glsl/spv), picked
    // at runtime by bgfx_get_renderer_type(); verify each backend below.
    init.type = BGFX_RENDERER_TYPE_VULKAN;
    init.platformData.nwh = hWnd;
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.debug = false;
    init.profile = false;
    init.callback = &s_xrCallback;

    if (!bgfx_init(&init))
    {
        LogInfo("! [BGFX] Failed to initialize bgfx");
        return false;
    }

    bgfx_set_view_rect(bgfxHDR::kSceneView, 0, 0, width, height);
    bgfx_set_view_clear(bgfxHDR::kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kSceneView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(bgfxHDR::kSceneView);

    m_bInitialized = true;
    LogInfo("[BGFX] Initialized: %dx%d, renderer: %s", width, height, bgfx_get_renderer_name(bgfx_get_renderer_type()));
    bgfxHDR::CreateHDRTarget((uint16_t)width, (uint16_t)height);

    return true;
}

void bgfxRenderDeviceRender::ShutdownBGFX()
{
    if (m_bInitialized)
    {
        bgfxHDR::DestroyHDRTarget();
        bgfx_shutdown();
        m_bInitialized = false;
    }
}
