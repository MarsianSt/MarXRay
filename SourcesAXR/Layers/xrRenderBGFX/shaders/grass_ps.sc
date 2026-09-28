$input v_texcoord0, v_viewPos, v_viewNormal, v_hemi
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_grassAlpha;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

// xmaterial, the value the reference actually gives grass.
//
// deffer_grass.ps:99-102 would have set the flora id here, but the assignment
// sits inside a preprocessor branch that never opens:
//     L99  #ifdef SSFX_FLORAFIX
//     L100 // Material value ( MAT_FLORA )
//     L101     ms = 0.15f;
//     L102 #endif
// SSFX_FLORAFIX is not defined anywhere in the reference shader tree - grepping
// every .h / .ps / .vs under game_unpacked\shaders finds `#ifdef SSFX_FLORAFIX`
// and `#if defined(ENCHANTED_SHADERS_ENABLED) && defined(SSFX_FLORAFIX)` uses
// (accum_base.ps:40/:78/:85, accum_omni_unshadowed.ps:32/:43, deffer_grass.ps:11
// and :99, deffer_tree_bump.vs:67, model_env_lq.vs:26) and not one `#define`, and
// the C++ that could add one as a shader option never mentions the name either
// (archive_sourse: the only hits are the ssfx_florafixes_1 / _2 constant
// bindings, Blender_Recorder_StandartBinding.cpp:579-580/:720-736/:1002-1003).
// So the arm is dead and the class falls through to the xmaterial every other
// writer uses - `float ms = xmaterial` at deffer_base_flat.ps:21, i.e.
// float(1.0h/4.h) under USE_R2_STATIC_SUN (common.h:17-18) - which is what
// deffer_grass.ps:113-121 then packs, through the same `ms` variable the dead
// branch would have written.
//
// The consequence is worth stating because the constants are still there:
// MAT_FLORA is 0.15f in common_brdf.h:16 and hmodel.h:28 / combine_1.ps:97 still
// test `abs(m - 0.15) <= 0.04`, but with 0.25 in the G-buffer neither can ever
// fire for grass - m_flora stays false, the SSS arm at lmodel.h:158-163 stays
// dormant, and the reference's own plant gloss fix at combine_1.ps:97-101 does
// not run on grass either. Taking 0.15 from the dead arm would have made all
// three light up, which is a fudge against the reference rather than a port of
// it.
const float GBUF_MTL = 0.25;

void main()
{
    vec4 base = texture2D(u_texture, v_texcoord0);
    if (u_grassAlpha.y > 0.5 && base.w < u_grassAlpha.x)
        discard;
    // Forward fog removed: single fog layer in combine (Anomaly).
    // deffer_grass.ps:74  surface_bumped S = sload(I);  -> S.base = tbase(I.tcdh)
    // and deffer_grass.ps:135  float4(S.base.rgb, S.gloss). The reference albedo
    // is the plain texture: the per-blade colour row c0 (c_sun, c_sun, c_sun,
    // c_hemi; DetailManager_VS.cpp:294) is read by deffer_grass.vs:115 for the
    // hemi only and never multiplies the diffuse, so nothing is multiplied here.
    gl_FragData[0] = base;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7), with the hemi
    // the vertex stage produced (deffer_grass.vs:115 -> I.position.w, read back
    // at deffer_grass.ps:118 `float h = I.position.w;`).
    //
    // The reference adds one more term to the normal here, deffer_grass.ps:70-79
    //   S.normal.xy *= max(5.0f * rain_params.y, 3.0f);
    //   float3 fN = mul(m_WV, float3(S.normal.x, 1.0f, S.normal.y));
    //   fN = normalize(fN);
    //   float3 Ne = float3(I.M1.z, I.M2.z, I.M3.z) + fN;   // I.M*.z = the eye-space
    //   Ne = normalize(Ne);                              // terrain normal of :97
    // i.e. it tilts the terrain normal by the *_bump map the shader binds
    // (details_blend.s:14  s_bump = "levels\\<lvl>\\" .. t_base .. "_bump"). That
    // map does not exist in this level's data (the jupiter archive holds only
    // levels/jupiter/build_details.dds, no build_details_bump), so the term has
    // no source here and is NOT replaced by a guessed constant.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, v_hemi, GBUF_MTL);
}
