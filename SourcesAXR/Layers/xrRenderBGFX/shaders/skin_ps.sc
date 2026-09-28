$input v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

// Dynamic models are the deffer_model_* / deffer_base_* pair in the reference
// and take xmaterial like any other static geometry: deffer_base_flat.ps:21
//     float ms = xmaterial;
// i.e. float(1.0h/4.h) under USE_R2_STATIC_SUN (common.h:17-18). The same value
// deffer_model_flat.vs:27-29 would override with L_material.y when
// USE_R2_STATIC_SUN && !USE_LM_HEMI, which that VS spells out; it is the R4
// per-object constant the port has no binding for, and the reference's own
// default for the class is the xmaterial above.
const float GBUF_MTL = 0.25;

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    // Forward fog removed: single fog layer in combine (Anomaly).
    gl_FragData[0] = c;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7). The model
    // class writes its hemi in the vertex stage from a hemi cube,
    // deffer_model_flat.vs:18-25
    //   float3  Nw        = mul((float3x3)m_W, (float3)I.N);
    //   float3  hc_pos    = (float3)hemi_cube_pos_faces;
    //   float3  hc_neg    = (float3)hemi_cube_neg_faces;
    //   float3  hc_mixed  = (Nw < 0) ? hc_neg : hc_pos;
    //   float   hemi_val  = dot(hc_mixed, abs(Nw));
    //   hemi_val          = saturate(hemi_val);
    //   O.position        = float4(Pe, hemi_val);
    // and the pixel stage only reads it back, deffer_base_flat.ps:41 (the
    // duplicate cube block in that PS at :29-40 is the commented-out one, the
    // VS block is the live one). Both halves of the cube are R4 constants this
    // port does not bind: hemi_cube_pos_faces / hemi_cube_neg_faces are filled
    // per object from CROS_impl::get_hemi_cube() (r4.h apply_lmaterial ->
    // RCache.hemi.set_pos_faces / set_neg_faces, R_Backend_hemi.cpp:16-26,
    // Blender_Recorder_StandartBinding.cpp:45-58/:938-939), a six-face
    // light-track LUT the object-specific stub only fakes with a constant
    // (bgfxRenderInterface.h:150), and reading that fake as a cube would be a
    // brightness fudge, not the reference. The hemi therefore stays the
    // normal's world-space up factor (gbuf_pack.h, calc_model_hemi_r1) until
    // the CROS hemi cube is ported.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z,
                                       gbuf_calc_hemi(v_viewNormal), GBUF_MTL);
}
