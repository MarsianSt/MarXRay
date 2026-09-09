#include "stdafx.h"
#include "../../xrCore/xrCore.h"
#include "../../Include/xrAPI/xrAPI.h"
#include "bgfxRenderFactory.h"
#include "bgfxRenderDeviceRender.h"
#include "bgfxDebugRender.h"
#include "bgfxRenderInterface.h"

#pragma comment(lib, "AdvancedXRay.lib")

// Render interface implementation
bgfxRenderInterface RenderImpl;
bgfxRenderTarget g_bgfxRenderTarget;

// Debug render implementation
bgfxDebugRender DebugRenderImpl;

// DU (draw utils) implementation - full CDUInterface vtable, all stubs
#ifndef ECORE_API
#define ECORE_API
#endif
#include "../../Include/xrRender/DrawUtils.h"

class bgfxDU final : public CDUInterface
{
public:
    virtual void __stdcall DrawCross(const Fvector&, float, float, float, float, float, float, u32, BOOL=false) override {}
    virtual void __stdcall DrawCross(const Fvector&, float, u32, BOOL=false) override {}
    virtual void __stdcall DrawFlag(const Fvector&, float, float, float, float, u32, BOOL) override {}
    virtual void __stdcall DrawRomboid(const Fvector&, float, u32) override {}
    virtual void __stdcall DrawJoint(const Fvector&, float, u32) override {}

    virtual void __stdcall DrawSpotLight(const Fvector&, const Fvector&, float, float, u32) override {}
    virtual void __stdcall DrawDirectionalLight(const Fvector&, const Fvector&, float, float, u32) override {}
    virtual void __stdcall DrawPointLight(const Fvector&, float, u32) override {}

    virtual void __stdcall DrawSound(const Fvector&, float, u32) override {}
    virtual void __stdcall DrawLineSphere(const Fvector&, float, u32, BOOL) override {}

    virtual void __stdcall dbgDrawPlacement(const Fvector&, int, u32, LPCSTR, u32) override {}
    virtual void __stdcall dbgDrawVert(const Fvector&, u32, LPCSTR) override {}
    virtual void __stdcall dbgDrawEdge(const Fvector&, const Fvector&, u32, LPCSTR) override {}
    virtual void __stdcall dbgDrawFace(const Fvector&, const Fvector&, const Fvector&, u32, LPCSTR) override {}

    virtual void __stdcall DrawFace(const Fvector&, const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) override {}
    virtual void __stdcall DrawLine(const Fvector&, const Fvector&, u32) override {}
    virtual void __stdcall DrawLink(const Fvector&, const Fvector&, float, u32) override {}
    virtual void __stdcall DrawFaceNormal(const Fvector&, const Fvector&, const Fvector&, float, u32) override {}
    virtual void __stdcall DrawFaceNormal(const Fvector*, float, u32) override {}
    virtual void __stdcall DrawFaceNormal(const Fvector&, const Fvector&, float, u32) override {}
    virtual void __stdcall DrawSelectionBox(const Fvector&, const Fvector&, u32*) override {}
    virtual void __stdcall DrawSelectionBoxB(const Fbox&, u32*) override {}
    virtual void __stdcall DrawIdentSphere(BOOL, BOOL, u32, u32) override {}
    virtual void __stdcall DrawIdentSpherePart(BOOL, BOOL, u32, u32) override {}
    virtual void __stdcall DrawIdentCone(BOOL, BOOL, u32, u32) override {}
    virtual void __stdcall DrawIdentCylinder(BOOL, BOOL, u32, u32) override {}
    virtual void __stdcall DrawIdentBox(BOOL, BOOL, u32, u32) override {}

    virtual void __stdcall DrawBox(const Fvector&, const Fvector&, BOOL, BOOL, u32, u32) override {}
    virtual void __stdcall DrawAABB(const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) override {}
    virtual void __stdcall DrawAABB(const Fmatrix&, const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) override {}
    virtual void __stdcall DrawOBB(const Fmatrix&, const Fobb&, u32, u32) override {}
    virtual void __stdcall DrawSphere(const Fmatrix&, const Fvector&, float, u32, u32, BOOL, BOOL) override {}
    virtual void __stdcall DrawSphere(const Fmatrix&, const Fsphere&, u32, u32, BOOL, BOOL) override {}
    virtual void __stdcall DrawCylinder(const Fmatrix&, const Fvector&, const Fvector&, float, float, u32, u32, BOOL, BOOL) override {}
    virtual void __stdcall DrawCone(const Fmatrix&, const Fvector&, const Fvector&, float, float, u32, u32, BOOL, BOOL) override {}
    virtual void __stdcall DrawPlane(const Fvector&, const Fvector2&, const Fvector&, u32, u32, BOOL, BOOL, BOOL) override {}
    virtual void __stdcall DrawPlane(const Fvector&, const Fvector&, const Fvector2&, u32, u32, BOOL, BOOL, BOOL) override {}
    virtual void __stdcall DrawRectangle(const Fvector&, const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) override {}

    virtual void __stdcall DrawGrid() override {}
    virtual void __stdcall DrawPivot(const Fvector&, float) override {}
    virtual void __stdcall DrawAxis(const Fmatrix&) override {}
    virtual void __stdcall DrawObjectAxis(const Fmatrix&, float, BOOL) override {}
    virtual void __stdcall DrawSelectionRect(const Ivector2&, const Ivector2&) override {}

    virtual void __stdcall DrawIndexedPrimitive(int, u32, const Fvector&, const Fvector*, const u32&, const u32*, const u32&, const u32&, float) override {}

    virtual void __stdcall OutText(const Fvector&, LPCSTR, u32, u32) override {}

    virtual void __stdcall OnDeviceDestroy() override {}
};

bgfxDU DUImpl;

// UI Render implementation
#include "../../Include/xrRender/UIRender.h"
#include "../../Include/xrRender/UIShader.h"
#include "bgfx_capi.h"
#include "bgfxUIProgram.h"
#include "bgfxUIShader.h"
#include "bgfxRenderInterface.h"

// Simple UI vertex format for bgfx
struct UIVertex
{
    float x, y, z;
    u32 color;
    float u, v;
};

static const u32 MAX_UI_VERTS = 8192;

// Global UI vertex layout
bgfx_vertex_layout_t g_uiVertexLayout;
static bool g_bUILayoutCreated = false;

// UI shader (texture) currently selected via SetShader
static bgfxUIShader* s_pCurrentUIShader = nullptr;

// Scissor state shared with the font renderer (bgfxFontRender)
static uint16_t s_scissorCache = 0xFFFF;
static bool s_scissorActive = false;

// Applies the current scissor to the next submit (call before bgfx_submit).
void bgfxUIScissorApply()
{
    bgfx_set_scissor_cached(s_scissorActive ? s_scissorCache : 0xFFFF);
}

class bgfxUIRender : public IUIRender
{
private:
    UIVertex m_vertices[MAX_UI_VERTS];
    u32 m_vertCount = 0;
    ePrimitiveType m_primType = ptNone;
    ePointType m_pointType = pttTL;
    bool m_bRendering = false;

    void CreateVertexLayout()
    {
        if (g_bUILayoutCreated)
            return;

        bgfx_vertex_layout_begin(&g_uiVertexLayout, bgfx_get_renderer_type());
        bgfx_vertex_layout_add(&g_uiVertexLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
        bgfx_vertex_layout_add(&g_uiVertexLayout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
        bgfx_vertex_layout_add(&g_uiVertexLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
        bgfx_vertex_layout_end(&g_uiVertexLayout);
        g_bUILayoutCreated = true;
    }

    void TransformToClipSpace()
    {
        u32 w = g_bgfxRenderTarget.m_width;
        u32 h = g_bgfxRenderTarget.m_height;
        if (0 == w || 0 == h)
        {
            w = 1024;
            h = 768;
        }

        const float invW = 2.0f / float(w);
        const float invH = 2.0f / float(h);

        for (u32 i = 0; i < m_vertCount; i++)
        {
            UIVertex& v = m_vertices[i];
            // XRay UI coords are top-left origin in pixels, shifted by -0.5
            // (D3D9 places vertices on pixel centers); bgfx/D3D11 places
            // them on pixel boundaries, so +0.5 restores the intended
            // pixel grid. NDC has y=+1 at top of the viewport.
            v.x = (v.x + 0.5f) * invW - 1.0f;
            v.y = 1.0f - (v.y + 0.5f) * invH;
        }
    }

public:
    virtual void CreateUIGeom() override
    {
        LogInfo("[BGFX] bgfxUIRender::CreateUIGeom() called");
        CreateVertexLayout();
        m_vertCount = 0;
        m_primType = ptNone;
        m_bRendering = false;
    }

    virtual void DestroyUIGeom() override
    {
        LogInfo("[BGFX] bgfxUIRender::DestroyUIGeom() called");
    }

    virtual void SetShader(IUIShader &shader) override
    {
        s_pCurrentUIShader = (bgfxUIShader*)&shader;
    }
    virtual void SetAlphaRef(int aref) override {}
    virtual void SetScissor(Irect* rect=NULL) override
    {
        if (rect && rect->x2 > rect->x1 && rect->y2 > rect->y1)
        {
            // X-Ray rects are inclusive on both ends
            s_scissorCache = bgfx_set_scissor((u16)rect->x1, (u16)rect->y1,
                (u16)(rect->x2 - rect->x1 + 1), (u16)(rect->y2 - rect->y1 + 1));
            s_scissorActive = true;
        }
        else
            s_scissorActive = false;
    }

    virtual void GetActiveTextureResolution(Fvector2 &res) override
    {
        if (s_pCurrentUIShader && s_pCurrentUIShader->GetWidth() > 0)
            res.set((float)s_pCurrentUIShader->GetWidth(), (float)s_pCurrentUIShader->GetHeight());
        else
            res.set(1024.0f, 1024.0f);
    }

    virtual void PushPoint(float x, float y, float z, u32 C, float u, float v) override
    {
        if (m_vertCount < MAX_UI_VERTS)
        {
            m_vertices[m_vertCount].x = x;
            m_vertices[m_vertCount].y = y;
            m_vertices[m_vertCount].z = z;
            // XRay packs color as 0xAARRGGBB; bgfx COLOR0 attribute reads
            // bytes as RGBA in memory, so swap R and B.
            m_vertices[m_vertCount].color =
                (C & 0xFF00FF00u) | ((C >> 16) & 0x000000FFu) | ((C << 16) & 0x00FF0000u);
            m_vertices[m_vertCount].u = u;
            m_vertices[m_vertCount].v = v;
            m_vertCount++;
        }
    }

    virtual void StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType) override
    {
        m_vertCount = 0;
        m_primType = primType;
        m_pointType = pointType;
        m_bRendering = true;
    }

    virtual void FlushPrimitive() override
    {
        if (!m_bRendering)
            return;

        // Movie UI statics (hud\movie shaders referencing .ogm) are drawn by
        // the video decoder itself — skip the white placeholder quad.
        if (s_pCurrentUIShader && s_pCurrentUIShader->IsMovie())
        {
            m_vertCount = 0;
            m_bRendering = false;
            return;
        }

        if (m_vertCount >= 3)
        {
            bgfx_program_handle_t prog = bgfxUITexturedProgramGet();
            if (bgfxUIProgramValid(prog))
            {
                TransformToClipSpace();

                bgfx_transient_vertex_buffer_t tvb;
                bgfx_alloc_transient_vertex_buffer(&tvb, m_vertCount, &g_uiVertexLayout);
                if (tvb.data)
                {
                    memcpy(tvb.data, m_vertices, m_vertCount * sizeof(UIVertex));

                    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
                    if (m_primType == ptTriStrip || m_primType == ptLineStrip)
                        state |= BGFX_STATE_PT_TRISTRIP;

                    bgfx_set_state(state, 0);
                    bgfx_set_transient_vertex_buffer(0, &tvb, 0, m_vertCount);
                    bgfx_uniform_handle_t sampler = bgfxUITextureSamplerGet();
                    bgfx_texture_handle_t tex;
                    if (s_pCurrentUIShader && bgfxIsValid(s_pCurrentUIShader->GetTexture()))
                        tex = s_pCurrentUIShader->GetTexture();
                    else
                        tex = bgfxUIWhiteTextureGet();
                    bgfx_set_texture(0, sampler, tex, UINT32_MAX);
                    bgfxUIScissorApply();
                    bgfx_submit(0, prog, 0, BGFX_DISCARD_ALL);
                }
            }
        }

        m_vertCount = 0;
        m_bRendering = false;
    }

    virtual LPCSTR UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name) override
    {
        // Mirror dxUIRender: video textures (.ogm) use the movie shader so
        // the UI skips drawing them (the video decoder renders the picture).
        string_path buff;
        if (tex_name && FS.exist(buff, "$game_textures$", tex_name, ".ogm"))
            return "hud\\movie";
        return sh_name;
    }
    virtual void CacheSetXformWorld(const Fmatrix& M) override {}
    virtual void CacheSetCullMode(CullMode) override {}
};

bgfxUIRender UIRenderImpl;

// Game material library placeholder
class CGameMtlLibrary {};
CGameMtlLibrary GMLibImpl;

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        LogInfo("[BGFX] DLL_PROCESS_ATTACH - Setting up render factory");
        ::Render = &RenderImpl;
        ::RenderFactory = &RenderFactoryImpl;
        ::DU = &DUImpl;
        ::UIRender = &UIRenderImpl;
        ::DRender = &DebugRenderImpl;
        ::PGMLib = &GMLibImpl;
        LogInfo("[BGFX] Render = %p, RenderFactory = %p", (void*)::Render, (void*)::RenderFactory);
        LogInfo("[BGFX] UIRender = %p, UIRenderImpl = %p", (void*)::UIRender, (void*)&UIRenderImpl);
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        LogInfo("[BGFX] DLL_PROCESS_DETACH");
        break;
    }
    return TRUE;
}

extern "C"
{
    bool _declspec(dllexport) SupportsVulkanRendering();
};

bool _declspec(dllexport) SupportsVulkanRendering()
{
    LogDebug("[BGFX] SupportsVulkanRendering() called");
    // BGFX with DX11 backend is supported on Windows
    return true;
}
