$input v_color0, v_texcoord0, v_viewPos, v_viewNormal

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

SAMPLER2D(s_base, 0);
uniform vec4 u_particleParams;

// xmaterial (deffer_particle.ps:66), i.e. the static material id
// (common.h:17-18).
const float GBUF_MTL = 0.25;

// deffer_particle.vs:26  O.position = float4(Pe, .2h) -> deffer_particle.ps:64
//     float4 Ne = float4(normalize((float3)I.N.xyz), I.position.w);
// The reference gives every particle quad a flat 0.2 hemi, a per-quad constant
// with no normal behind it (a billboard has no facing: deffer_particle.vs:23
// points O.N at the camera), and spends it as the hscale of the same
// L_hemi_color the other classes use (hmodel.h:109 / :125).
const float GBUF_HEMI = 0.2;

void main()
{
    vec4 texColor = texture2D(s_base, v_texcoord0);
    vec4 c = texColor * v_color0;
    if (u_particleParams.y > 0.5 && c.a * 255.0 < u_particleParams.x)
        discard;
    gl_FragData[0] = c;
    // Position G-buffer (Anomaly gbuf position): view-space position. The rain
    // pass (bgfxRainRender.cpp) shares this program, so streaks write it too.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7).
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, GBUF_HEMI);
}
