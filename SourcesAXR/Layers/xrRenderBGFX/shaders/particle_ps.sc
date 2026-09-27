$input v_color0, v_texcoord0, v_viewPos, v_viewNormal

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

SAMPLER2D(s_base, 0);
uniform vec4 u_particleParams;

// xmaterial (deffer_particle.ps:66), i.e. the static material id
// (common.h:17-18).
const float GBUF_MTL = 0.25;

// deffer_particle.vs:22  O.position = float4(Pe, .2h) - the reference gives every
// particle a flat 0.2 hemi, so this one needs no environment source.
const float GBUF_PARTICLE_HEMI = 0.2;

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
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, GBUF_PARTICLE_HEMI);
}
