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
    // gives every blade clamp(c0.w, 0.05f, 1.0f) (deffer_grass.vs:115), read
    // back as I.position.w (deffer_grass.ps:118). c0 is array[i+3] out of the
    // 61*4-float4 `array` constant the detail manager dumps per grass batch
    // (deffer_grass.vs:14 / DetailManager_VS.cpp:174,213), i.e. a per-blade CPU
    // value - the port's grass vertex layout carries position, colour, tc0 and
    // tc1 only (bgfxDetails.cpp:424-427) and binds no such constant, so the
    // reference source is not reachable here and the hemi stays the normal's
    // world-space up factor (gbuf_pack.h, calc_model_hemi_r1). Note the
    // reference itself flags the constant as unreliable at the same line
    // ("Some spots are bugged (Full black)") and no shipped config overrides it.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, gbuf_calc_hemi(v_viewNormal));
}
