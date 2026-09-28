$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_fog
#include <bgfx_shader.sh>

// AXR particle.vs (r3/particle.vs:26-43). The reference vertex stage for every
// forward particle pass, i.e. everything CBlender_Particle and the
// particles_*.s materials bind except oBlend==0 (deffer_particle). Three things
// reach the pixel stage:
//   o.hpos = mul(m_WVP, v.P)      - world-space input, m_WVP is view*proj
//   o.tc / o.c                    - copied through (r3/particle.vs:31-32)
//   o.fog = saturate(calc_fogging(v.P))   (r3/particle.vs:41)
//
// calc_fogging (r2/common.h:72) is dot(w_pos, fog_plane), the world-space
// linear fog_near/fog_far ramp bound by cl_fog_plane
// (Blender_Recorder_StandartBinding.cpp:138-162) - NOT the port's screen-space
// u_fogParams of combine_ps.sc, which is a different quantity. It is
// per-vertex, so it interpolates across the quad, exactly as in the reference.
uniform vec4 u_fogPlane;

void main()
{
    vec4 wpos = vec4(a_position, 1.0);
    gl_Position = mul(u_modelViewProj, wpos);
    v_color0    = a_color0;
    v_texcoord0 = a_texcoord0;
    v_fog       = saturate(dot(wpos, u_fogPlane));
}
