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
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7). Hemi comes
    // from u_gbufHemi because the reference's per-blade constant c0.w
    // (deffer_grass.vs:115) is not bound by the port.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, u_gbufHemi.x);
}
