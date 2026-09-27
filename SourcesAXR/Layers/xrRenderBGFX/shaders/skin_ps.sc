$input v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

// Dynamic models are the deffer_model_* / deffer_base_* pair in the reference
// and take xmaterial like any other static geometry (deffer_base_flat.ps:19).
const float GBUF_MTL = 0.25;

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    // Forward fog removed: single fog layer in combine (Anomaly).
    gl_FragData[0] = c;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7). For models the
    // reference's hemi-cube block (deffer_model_flat.vs:19-30) is commented out
    // in deffer_base_flat.ps:29-40, so this class too reads the per-vertex
    // I.position.w, which the port does not bind; the hemi is the normal's
    // world-space up factor (gbuf_pack.h, calc_model_hemi_r1).
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, gbuf_calc_hemi(v_viewNormal));
}
