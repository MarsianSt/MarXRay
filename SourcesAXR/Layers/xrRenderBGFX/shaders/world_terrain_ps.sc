$input v_texcoord0, v_texcoord1, v_viewPos, v_viewNormal, v_hemi

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_alphaCtrl;
uniform vec4 u_dtScale;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);
SAMPLER2D(u_mask,   1);
SAMPLER2D(u_dt0,    2);
SAMPLER2D(u_dt1,    3);
SAMPLER2D(u_dt2,    4);
SAMPLER2D(u_dt3,    5);

// Terrain material id, deffer_terrain_mid_flat.ps:56 / deffer_terrain_low_flat.ps:22
// / deffer_impl_flat.ps:210 all pass a literal 0.95f as the mtl of pack_gbuffer.
const float GBUF_MTL = 0.95;

void main()
{
    vec4 base = texture2D(u_texture, v_texcoord0);
    vec3 color = base.rgb;
    if (u_dtScale.x > 0.5)
    {
        vec4 mask = texture2D(u_mask, v_texcoord0);
        mask /= max(mask.r + mask.g + mask.b + mask.a, 0.00001);
        vec2 uv = v_texcoord0 * u_dtScale.xy;
        vec3 det = texture2D(u_dt0, uv).rgb * mask.r
                 + texture2D(u_dt1, uv).rgb * mask.g
                 + texture2D(u_dt2, uv).rgb * mask.b
                 + texture2D(u_dt3, uv).rgb * mask.a;
        color = 2.0 * base.rgb * det;
    }
    // Forward fog removed: single fog layer in combine (Anomaly).
    gl_FragData[0] = vec4(color, 1.0);
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7). The reference
    // reads the terrain hemi from D.w, i.e. the per-vertex I.Nh.w
    // (deffer_terrain_flat_d.vs:19 / :26 -> deffer_terrain_mid_flat.ps:53
    // float4 Ne = float4(normalize(N), D.w)), which the vertex stage hands over
    // in v_hemi.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, v_hemi);
}
