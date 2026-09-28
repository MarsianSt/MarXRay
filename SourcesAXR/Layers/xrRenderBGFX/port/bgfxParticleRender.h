#pragma once

#include "stdafx.h"
#include "../bgfx_capi.h"
#include "bgfxHDR.h"

namespace PS { class CPEDef; }
namespace PAPI { struct Particle; }

bool bgfxLoadWorldTexture(LPCSTR texName, bgfx_texture_handle_t& outTex,
    unsigned int& outW, unsigned int& outH);

namespace bgfxParticles
{

    // The scene FX view: the last writer into the G-buffer, and where the
    // reference's emissive and priority-1 geometry ends up (r4_R_render.cpp:538,
    // :546, :633). The id lives in port/bgfxHDR.h so the renderer can order it.
    using bgfxHDR::kSceneFxView;
    const bgfx_view_id_t kView = bgfxHDR::kSceneFxView;

enum BlendMode
{
    BLEND_SET       = 0,
    BLEND_BLEND     = 1,
    BLEND_ADD       = 2,
    BLEND_MUL       = 3,
    BLEND_MUL_2X    = 4,
    BLEND_ALPHA_ADD = 5
};

struct Vertex
{
    Fvector  pos;
    u32      color;
    Fvector2 uv;
};

void OnDeviceCreate();
void OnDeviceDestroy();

bgfx_texture_handle_t GetTexture(LPCSTR name);

u64 BlendState(int blendMode, bool writeZ);

// Maps a particle definition's shader name (CPEDef::m_ShaderName, e.g.
// "particles\blend", "particles\add", "particles\alpha_add") onto a BlendMode.
// The real blend token lives in the compiled shader pass (CBlender_Particle),
// which the BGFX port does not build, so this is a name-based heuristic.
// Unknown names fall back to BLEND_BLEND.
int BlendFromShaderName(LPCSTR shaderName);

// Which forward pixel stage a particle material's shader name selects, i.e.
// which PS CBlender_Particle::Compile / the particles_*.s materials bind for that
// material (Blender_Particle.cpp:127-131 and particles_add.s:2 /
// particles_xadd.s:2). The additive pair is a separate program in the reference
// because only it spends the fog on rgb as well (particle_add.ps:43).
enum ForwardPixel
{
    FWD_PIXEL_NONE = -1,   // no forward stage: the effect is deferred (BLEND_SET)
    FWD_PIXEL_BLEND = 0,   // particle.ps       (BLEND / MUL / MUL_2X / ALPHA-ADD)
    FWD_PIXEL_ADD   = 1    // particle_add.ps   (ADD: particles\add, particles\xadd)
};

ForwardPixel ForwardPixelForShader(LPCSTR shaderName);

// Binds the target the forward pass draws into: the combine result (AXR
// render_forward's rt_Generic_0, r4_rendertarget_phase_combine.cpp:378) with
// the scene depth attached (HW.pBaseZB in the same u_setrt), so the quads are
// depth-tested against the geometry the deferred pass resolved. Until the HDR
// module hands one over, the pass stays inactive and the blended effects keep
// the deferred route, i.e. the frame is exactly what it was before this path
// existed.
void SetForwardFrameBuffer(bgfx_frame_buffer_handle_t fb);

// Draws the queued forward particle effects and empties the queue. Call it
// where CRender::render_forward is called, i.e. from phase_combine after the
// tonemap (r4_rendertarget_phase_combine.cpp:374-388 -> r4_R_render.cpp:617-640).
void RenderForwardPass();

void BuildBillboardQuad(Vertex out[4], const Fvector& center,
    const Fvector& axisT, const Fvector& axisR,
    float r_x, float r_y, float sina, float cosa,
    const Fvector2& lt, const Fvector2& rb, u32 color);

void BuildCameraBillboard(Vertex out[4], const Fvector& center,
    float r_x, float r_y, float sina, float cosa,
    const Fvector2& lt, const Fvector2& rb, u32 color);

bool Submit(bgfx_texture_handle_t tex, const Vertex* quads, u32 quadCount,
    int blendMode, bool writeZ, bool alphaTest, u8 alphaRef,
    const Fmatrix* projOverride = nullptr);

// Builds the billboards of one effect and routes them the way the reference
// splits them (CBlender_Particle::Compile, Blender_Particle.cpp:122-132): only
// oBlend==0 (SET) is deferred into the G-buffer, every other blend mode is a
// forward effect drawn after the combine. shaderName is CPEDef::m_ShaderName
// and picks the forward pixel stage.
bool SubmitPAPI(PAPI::Particle* particles, u32 count, PS::CPEDef* def,
    int blendMode = BLEND_BLEND, const Fmatrix* xformOrNull = nullptr,
    const Fmatrix* projOverride = nullptr, LPCSTR shaderName = nullptr);

}
