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
    // gives every blade clamp(c0.w, 0.05f, 1.0f) (deffer_grass.vs:115), a
    // per-blade constant the port does not bind - and the reference itself notes
    // it as "Some spots are bugged (Full black)" and offers v_hemi(N) as the
    // fix, so the hemi here is per pixel from the normal (gbuf_pack.h,
    // calc_model_hemi_r1): a leaf lit from the sky side keeps its value while the
    // leaves turned away from it drop to L_ambient, instead of every blade
    // sharing one frame-wide constant.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, gbuf_calc_hemi(v_viewNormal));
}
