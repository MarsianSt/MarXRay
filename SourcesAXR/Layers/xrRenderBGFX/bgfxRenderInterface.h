#pragma once

#include "stdafx.h"
#include "bgfx_capi.h"
#include "bgfxUIState.h"
#include "..\..\Include\xrRender\RenderFactory.h"

#ifndef ENGINE_API
#define ENGINE_API
#endif

#include "..\..\xrEngine\Render.h"
#include "bgfxUIShader.h"
#include "port\bgfxModelBridge.h"
#include "port\bgfxDetails.h"
#include "port\bgfxHDR.h"
#include "port\bgfxWallMarks.h"

class bgfxRenderTarget : public IRender_Target
{
public:
    u32 m_width = 0;
    u32 m_height = 0;

    virtual void set_blur(float f) override {}
    virtual void set_gray(float f) override {}
    virtual void set_duality_h(float f) override {}
    virtual void set_duality_v(float f) override {}
    virtual void set_noise(float f) override {}
    virtual void set_noise_scale(float f) override {}
    virtual void set_noise_fps(float f) override {}
    virtual void set_color_base(u32 f) override {}
    virtual void set_color_gray(u32 f) override {}
    virtual void set_color_add(const Fvector &f) override {}
    virtual u32 get_width() override { return m_width; }
    virtual u32 get_height() override { return m_height; }
    virtual void set_cm_imfluence(float f) override {}
    virtual void set_cm_interpolate(float f) override {}
    virtual void set_cm_textures(const shared_str &tex0, const shared_str &tex1) override {}
};

extern bgfxRenderTarget g_bgfxRenderTarget;

// ---------------------------------------------------------------------------
// Stub dynamic light: implements all IRender_Light virtuals so the game can
// create/release lights safely (state is stored CPU-side only — no GPU).
// ---------------------------------------------------------------------------
class bgfxLight : public IRender_Light
{
public:
    LT        m_type         = POINT;
    bool      m_active       = false;
    bool      m_shadow       = false;
    bool      m_volumetric   = false;
    float     m_volQuality   = 1.f;
    float     m_volIntensity = 1.f;
    float     m_volDistance  = 1.f;
    Fvector   m_position     = {0.f, 0.f, 0.f};
    Fvector   m_direction    = {0.f, 0.f, 1.f};
    Fvector   m_right        = {1.f, 0.f, 0.f};
    float     m_cone         = 0.5f;
    float     m_range        = 10.f;
    float     m_virtualSize  = 10.f;
    float     m_brightness   = 1.f;
    shared_str m_texture;
    Fcolor    m_color        = {1.f, 1.f, 1.f, 1.f};
    bool      m_hudMode      = false;
    bool      m_moveable     = false;
    bool      m_flare        = false;

    virtual void set_type(LT type) override { m_type = type; }
    virtual void set_active(bool b) override { m_active = b; }
    virtual bool get_active() override { return m_active; }
    virtual void set_shadow(bool b) override { m_shadow = b; }
    virtual void set_volumetric(bool b) override { m_volumetric = b; }
    virtual void set_volumetric_quality(float f) override { m_volQuality = f; }
    virtual void set_volumetric_intensity(float f) override { m_volIntensity = f; }
    virtual void set_volumetric_distance(float f) override { m_volDistance = f; }
    virtual void set_position(const Fvector& P) override { m_position = P; }
    virtual void set_rotation(const Fvector& D, const Fvector& R) override { m_direction = D; m_right = R; }
    virtual void set_cone(float angle) override { m_cone = angle; }
    virtual void set_range(float R) override { m_range = R; }
    virtual float get_range() const override { return m_range; }
    virtual void set_virtual_size(float R) override { m_virtualSize = R; }
    virtual void set_texture(LPCSTR name) override { m_texture = name; }
    virtual void set_color(const Fcolor& C) override { m_color = C; m_brightness = C.intensity(); }
    virtual void set_color(float r, float g, float b) override { m_color.set(r, g, b, 1.f); m_brightness = m_color.intensity(); }
    virtual void set_hud_mode(bool b) override { m_hudMode = b; }
    virtual bool get_hud_mode() override { return m_hudMode; }
    virtual void set_moveable(bool b) override { m_moveable = b; }
    virtual void set_flare(bool b) override { m_flare = b; }
};

// ---------------------------------------------------------------------------
// Stub dynamic glow.
// ---------------------------------------------------------------------------
class bgfxGlow : public IRender_Glow
{
public:
    bool      m_active      = false;
    Fvector   m_position    = {0.f, 0.f, 0.f};
    Fvector   m_direction   = {0.f, 0.f, 1.f};
    float     m_radius      = 1.f;
    shared_str m_texture;
    Fcolor    m_color       = {1.f, 1.f, 1.f, 1.f};

    virtual void set_active(bool b) override { m_active = b; }
    virtual bool get_active() override { return m_active; }
    virtual void set_position(const Fvector& P) override { m_position = P; }
    virtual void set_direction(const Fvector& P) override { m_direction = P; }
    virtual void set_radius(float R) override { m_radius = R; }
    virtual void set_texture(LPCSTR name) override { m_texture = name; }
    virtual void set_color(const Fcolor& C) override { m_color = C; }
    virtual void set_color(float r, float g, float b) override { m_color.set(r, g, b, 1.f); }
};

// ---------------------------------------------------------------------------
// Stub object-specific info: implements all IRender_ObjectSpecific virtuals so
// the game can call ROS()->* safely (state stored CPU-side only).
// ---------------------------------------------------------------------------
class bgfxObjectSpecific : public IRender_ObjectSpecific
{
public:
    u32   m_mode      = TRACE_ALL;
    float m_lum       = 1.f;
    float m_hemi      = 1.f;
    float m_hemi_cube[6] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };

    virtual void force_mode(u32 mode) override { m_mode = mode; }
    virtual float get_luminocity() override { return m_lum; }
    virtual float get_luminocity_hemi() override { return m_hemi; }
    virtual float* get_luminocity_hemi_cube() override { return m_hemi_cube; }
};

class bgfxRenderInterface : public IRender_interface
{
public:
    bgfxRenderInterface() = default;
    virtual ~bgfxRenderInterface() override = default;

    // Feature level
    virtual GenerationLevel get_generation() override { return GENERATION_R2; }
    virtual bool is_sun_static() override { return true; }
    virtual DWORD get_dx_level() override { return 0x00009000; }

    // Loading / Unloading
    virtual void create() override {}
    virtual void destroy() override {}
    virtual void reset_begin() override {}
    virtual void reset_end() override {}

    virtual void level_Load(IReader* fs) override
    {
        bgfxLoadGeometry();
        bgfxLoadVisuals(fs);
        bgfxDetailsLoad();
    }
    virtual void level_Unload() override { bgfxDetailsUnload(); }

    virtual HRESULT shader_compile(LPCSTR, DWORD const*, UINT, LPCSTR, LPCSTR, DWORD, void*&) override
    {
        return E_FAIL;
    }

    // Information
    virtual void Statistics(CGameFont* F) override {}

    virtual LPCSTR getShaderPath() override { return "shaders"; }
    virtual IRender_Sector* getSector(int id) override { return nullptr; }
    virtual IRenderVisual* getVisual(int id) override { return static_cast<IRenderVisual*>(bgfxGetVisual(id)); }
    virtual IRender_Sector* detectSector(const Fvector& P) override { return nullptr; }
    virtual IRender_Target* getTarget() override { return &g_bgfxRenderTarget; }

    virtual SurfaceParams getSurface(const char* nameTexture) override { return SurfaceParams(); }

    // Main
    virtual void set_Transform(Fmatrix* M) override { if (M) m_transform = *M; }
    virtual void set_HUD(BOOL V) override { m_hud = V; }
    virtual BOOL get_HUD() override { return m_hud; }
    virtual void set_Invisible(BOOL V) override { m_invisible = V; }
    virtual void Render3DStatic() override {}
    virtual void set_UI(BOOL V) override { m_ui = V; }
    virtual void flush() override {}
    virtual void set_Object(IRenderable* O) override { m_object = O; }
    virtual void add_Occluder(Fbox2& bb_screenspace) override { (void)bb_screenspace; }
    virtual void add_Visual(IRenderVisual* V, bool ignore_opt = false) override
    {
        (void)ignore_opt;
        if (!V || m_invisible)
            return;
        bgfxAddDynamicVisual(V, &m_transform._11, m_hud, m_invisible);
    }
    virtual void add_Geometry(IRenderVisual* V) override { (void)V; }
    virtual void add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V) override
    {
        if (!T || !V || T->suppress_wm)
            return;
        bgfxUIShader* sh = (bgfxUIShader*)&*S;
        if (!sh)
            return;
        bgfx_texture_handle_t tex = sh->GetTexture();
        if (!bgfxIsValid(tex))
            return;
        bgfxWallMarks::AddWallmark(T, V, P, tex, s);
    }
    virtual void add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V) override
    {
        if (!pArray || !T || !V || T->suppress_wm)
            return;
        wm_shader S = pArray->GenerateWallmark();
        add_StaticWallmark(S, P, s, T, V);
    }
    virtual void clear_static_wallmarks() override { bgfxWallMarks::Clear(); }
    // Blood on animated meshes (skeleton wallmarks) is not implemented yet.
    virtual void add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start, const Fvector& dir, float size) override {}

    // Object specific
    virtual IRender_ObjectSpecific* ros_create(IRenderable* parent) override { (void)parent; return xr_new<bgfxObjectSpecific>(); }
    virtual void ros_destroy(IRender_ObjectSpecific*& p) override { xr_delete(p); }

    // Lighting/glowing
    virtual IRender_Light* light_create() override { return xr_new<bgfxLight>(); }
    virtual void light_destroy(IRender_Light* p_) override {}
    virtual IRender_Glow* glow_create() override { return xr_new<bgfxGlow>(); }
    virtual void glow_destroy(IRender_Glow* p_) override {}

    // Models
    virtual IRenderVisual* model_CreateParticles(LPCSTR name) override { return static_cast<IRenderVisual*>(bgfxModelCreateParticles(name)); }
    virtual IRenderVisual* model_Create(LPCSTR name, IReader* data = 0) override
    {
        return static_cast<IRenderVisual*>(bgfxModelCreate(name));
    }
    virtual IRenderVisual* model_CreateChild(LPCSTR name, IReader* data) override { return static_cast<IRenderVisual*>(bgfxModelCreateChild(name, data)); }
    virtual IRenderVisual* model_Duplicate(IRenderVisual* V) override { return static_cast<IRenderVisual*>(bgfxModelDuplicate(V)); }
    virtual void model_Delete(IRenderVisual*& V, BOOL bDiscard = FALSE) override { bgfxModelDelete(reinterpret_cast<void**>(&V), bDiscard); }
    virtual void model_Logging(BOOL bEnable) override {}
    virtual void models_Prefetch() override {}
    virtual void models_Clear(BOOL b_complete) override {}

    // Occlusion culling
    virtual BOOL occ_visible(vis_data& V) override { return TRUE; }
    virtual BOOL occ_visible(Fbox& B) override { return TRUE; }
    virtual BOOL occ_visible(sPoly& P) override { return TRUE; }

    // Main render
    virtual void Calculate() override {}
    virtual void Render() override
    {
        static u32 s_calls = 0;
        if (s_calls < 3)
            LogInfo("[BGFX] IRender_interface::Render() pass");
        ++s_calls;
        // R2 combine: sky/clouds are the background, submitted before the world.
        bgfxRenderEnvironmentSky();
        bgfxRenderSceneObjects();
        bgfxRenderWorld();
        bgfxDetailsRender();    // grass / detail objects (level.details)
        bgfxWallMarks::Render();    // bullet holes / blood on level geometry
        bgfxRenderDynamic(0);   // world dynamics: flush CPU-side visuals to the port
        // R2 combine (flares over scene) + R2 forward (rain/thunder after sorted).
        bgfxRenderEnvironmentFx();
        bgfxHDR::CombinePass((u16)Device.dwWidth, (u16)Device.dwHeight);
        if (currentViewPort == MAIN_VIEWPORT)
            bgfxRenderHudPass();    // actor hands + weapon (see bgfxRenderCompat)
        bgfxClearDynamic();
    }

    // [FFT++]
    virtual void BeforeWorldRender() override {}
    virtual void AfterWorldRender() override {}

    // Screenshot
    virtual void Screenshot(ScreenshotMode mode = SM_NORMAL, LPCSTR name = 0) override
    {
        if (mode != SM_NORMAL)
            return; // gamesave/mp modes need DDS — not implemented yet

        static u32 s_ssCount = 0;
        char buf[64];
        xr_sprintf(buf, sizeof(buf), "ssq_%u.tga", ++s_ssCount);
        bgfx_frame_buffer_handle_t fb;
        fb.idx = 0xFFFF; // default framebuffer
        bgfx_request_screen_shot(fb, buf);    }
    virtual void Screenshot(ScreenshotMode mode, CMemoryWriter& memory_writer) override {}
    virtual void ScreenshotAsyncBegin() override {}
    virtual void ScreenshotAsyncEnd(CMemoryWriter& memory_writer) override {}

    virtual void CreatePanorama() override {}

    // Render mode
    virtual void rmNear() override {}
    virtual void rmFar() override {}
    virtual void rmNormal() override {}
    virtual u32 memory_usage() override { return 0; }

    virtual u32 active_phase() override { return 0; }
    virtual void RenderToTarget(RRT target) override {}

    virtual bool isActorShadowEnabled() override { return false; }

    virtual void RenderApplyRTandZB() override {}

protected:
    Fmatrix       m_transform  = Fidentity;   // set_Transform
    BOOL          m_hud        = FALSE;       // set_HUD / get_HUD
    BOOL          m_invisible  = FALSE;       // set_Invisible
    BOOL          m_ui         = FALSE;       // set_UI
    IRenderable*  m_object     = nullptr;     // set_Object

    virtual void ScreenshotImpl(ScreenshotMode mode, LPCSTR name, CMemoryWriter* memory_writer) override {}
};
