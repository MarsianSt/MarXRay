$input v_texcoord0, v_texcoord1, v_viewPos, v_viewNormal

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_alphaCtrl;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;
// 1 while a level lightmap is bound for this draw, 0 otherwise. The reference
// makes the same choice at shader-compile time instead of per draw:
// uber_deffer.cpp:16-24 recognises a material whose third texture starts with
// "lmap", :113-117 then defines USE_LM_HEMI, and the writer it picks is
// deffer_*_lmh_* rather than deffer_*. bgfx compiles one program for the whole
// world pass, so the permutation travels as this uniform.
uniform vec4 u_lmapValid;

SAMPLER2D(u_texture, 0);
// The level lightmap: the AXR `s_hemi` of deffer_base_flat.ps:23, i.e.
// CBlender_Compile::L_textures[2] (uber_deffer.cpp:182-185 / :211). Sampler
// stage 1, the next free stage after the diffuse, on the terrain path the mask
// - the two never share a draw.
SAMPLER2D(u_lmap, 1);

// xmaterial with USE_R2_STATIC_SUN (common.h:17-18); the static and decal
// classes share it in the reference (deffer_base_flat.ps:19, :54).
const float GBUF_MTL = 0.25;

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    if (u_alphaCtrl.y > 0.5 && c.a < u_alphaCtrl.x)
        discard;
    // Forward fog removed: screenspace fog in combine is the single layer
    // (as in Anomaly); keeping both gives 2f-f^2 overfog.
    // gl_FragData[0] instead of gl_FragColor: once the PS declares a second
    // output shaderc stops emitting the gl_FragColor/SV_TARGET0 alias.
    gl_FragData[0] = c;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7):
    // XY = packed normal, Z = view-space z, W = hemi.
    //
    // The hemi is the AXR USE_LM_HEMI branch, deffer_base_flat.ps:22-27:
    //     float4 lm = s_hemi.Sample( smp_rtlinear, I.lmh );
    //     float  h  = get_hemi(lm);
    // get_hemi (common_functions.h:129-137) is lm.a unless USE_SHOC_MODE is
    // defined, and nothing in the r3 tree defines it - so the hemi scalar is the
    // lightmap's alpha channel, the baked indirect light the level compiler
    // wrote there, and NOT max(0, Nw.y). That value is what hmodel.h:109 spends
    // as `hscale` (env_d *= light.xxx, hmodel.h:125), so the level's baked
    // lighting reaches the resolve through exactly the ambient term the
    // reference drives it through, and nothing else has to change downstream.
    //
    // The sampler is smp_rtlinear - RT filtering, i.e. linear inside the tile and
    // REPEAT across tiles (common_samplers.h), which is what the lightmap needs:
    // the uv runs over the whole texture (unpack_tc_lmap maps [-1..1] to [0..1],
    // common_functions.h:112) and its outer border is the level's ambient
    // falloff, so it must wrap, not clamp (uber_deffer.cpp:211's D3DTADDRESS_*
    // for the DX9 path is CLAMP, but the DX10 path it is paired with there,
    // C.r_dx10Texture, leaves the sampler state alone - see
    // Blender_Recorder_StandartBinding for the smp_rtlinear default).
    // Sampled unconditionally and selected afterwards, because texture2D takes
    // implicit derivatives and those are only defined in uniform control flow.
    // With nothing bound at stage 1 the fetch is 0 and u_lmapValid.x is 0, so
    // the mix reproduces the fallback exactly.
    vec4 lm = texture2D(u_lmap, v_texcoord1);
    float hemi = mix(gbuf_calc_hemi(v_viewNormal), lm.a, u_lmapValid.x);
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, hemi);
}
