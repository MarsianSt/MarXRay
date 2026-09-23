#pragma once

#include "stdafx.h"
#include "../bgfx_capi.h"

namespace PS { class CPEDef; }
namespace PAPI { struct Particle; }

bool bgfxLoadWorldTexture(LPCSTR texName, bgfx_texture_handle_t& outTex,
    unsigned int& outW, unsigned int& outH);

namespace bgfxParticles
{

const bgfx_view_id_t kView = 2;

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

bool SubmitPAPI(PAPI::Particle* particles, u32 count, PS::CPEDef* def,
    int blendMode = BLEND_BLEND, const Fmatrix* xformOrNull = nullptr,
    const Fmatrix* projOverride = nullptr);

}
