// BGFX Renderer - uses bgfx C API for C++17 compatibility
#include "stdafx.h"
#include "bgfxRenderDeviceRender.h"
#include "bgfxRenderInterface.h"
#include "bgfx_capi.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

        // Create a D3D11 device for ImGui DX11 backend
        // (bgfx doesn't expose its internal D3D11 device)
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        ID3D11Device* pd3dDevice = nullptr;
        ID3D11DeviceContext* pd3dContext = nullptr;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            &featureLevel, 1, D3D11_SDK_VERSION,
            &pd3dDevice, nullptr, &pd3dContext);
        if (SUCCEEDED(hr) && pd3dDevice && pd3dContext)
        {
            ImGui_ImplDX11_Init(pd3dDevice, pd3dContext);
            LogInfo("[BGFX] ImGui DX11 backend initialized");
        }
        else
        {
            LogError("[BGFX] Failed to create D3D11 device for ImGui, hr=0x%08X", hr);
        }

        g_bImGuiInitialized = true;
        LogInfo("[BGFX] ImGui initialized");
    }

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

    bgfx_set_view_rect(0, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_set_view_clear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    // Preserve submission order within the UI view (dialogs must draw over menu).
    bgfx_set_view_mode(0, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(0);

    // View 1 (no clear) renders after all view 0 submits — the intro video
    // must draw on top of the UI "back" quad that covers the screen.
    bgfx_set_view_rect(1, 0, 0, (uint16_t)m_width, (uint16_t)m_height);
    bgfx_touch(1);
}

void bgfxRenderDeviceRender::Clear()
{
    bgfx_set_view_clear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    bgfx_touch(0);
}

void bgfxRenderDeviceRender::End()
{
}

void bgfxRenderDeviceRender::ClearTarget()
{
}

void bgfxRenderDeviceRender::SetCacheXform(Fmatrix &mView, Fmatrix &mProject)
{
    bgfx_set_view_transform(0, mView.m, mProject.m);
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
    bgfx_frame(BGFX_FRAME_NONE);
}

bool bgfxRenderDeviceRender::InitBGFX(HWND hWnd, u32 width, u32 height)
{
    if (m_bInitialized)
        return true;

    bgfx_init_t init;
    bgfx_init_ctor(&init);
    init.type = BGFX_RENDERER_TYPE_DIRECT3D11;
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

    bgfx_set_view_rect(0, 0, 0, width, height);
    bgfx_set_view_clear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    bgfx_set_view_mode(0, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_touch(0);

    m_bInitialized = true;
    LogInfo("[BGFX] Initialized: %dx%d, renderer: %s", width, height, bgfx_get_renderer_name(bgfx_get_renderer_type()));

    return true;
}

void bgfxRenderDeviceRender::ShutdownBGFX()
{
    if (m_bInitialized)
    {
        bgfx_shutdown();
        m_bInitialized = false;
    }
}
