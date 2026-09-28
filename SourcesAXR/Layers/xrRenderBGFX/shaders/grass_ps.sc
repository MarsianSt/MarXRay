$input v_color0, v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_grassAlpha;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

// MAT_FLORA, the material deffer_grass.ps:99-102 assigns to grass under
// SSFX_FLORAFIX (common_brdf.h:16 MAT_FLORA 0.15f); combine_1.ps:97 and the
// screen-space AO/IL passes branch on exactly this id.
const float GBUF_MTL = 0.15;

void main()
{
    vec4 base = texture2D(u_texture, v_texcoord0);
    if (u_grassAlpha.y > 0.5 && base.w < u_grassAlpha.x)
        discard;
    vec4 c = vec4(base.rgb * v_color0.rgb, base.w * v_color0.a);
    // Forward fog removed: single fog layer in combine (Anomaly).
    gl_FragData[0] = c;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7). The reference
    // adds one more term to the normal here, deffer_grass.ps:70-79
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
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, gbuf_calc_hemi(v_viewNormal), GBUF_MTL);
}
