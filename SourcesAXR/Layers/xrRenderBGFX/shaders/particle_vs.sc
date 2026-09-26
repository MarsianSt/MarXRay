$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_viewPos
#include <bgfx_shader.sh>

void main()
{
    // u_modelViewProj may carry a projection override (bgfxParticleRender.cpp
    // SetupView), so the view-space position is built separately for the
    // position G-buffer instead of being read back out of gl_Position.
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0    = a_color0;
    v_texcoord0 = a_texcoord0;
    // Position G-buffer (Anomaly gbuf position): view-space position. The rain
    // pass (bgfxRainRender.cpp) shares this program, so streaks write it too.
    v_viewPos   = viewPos.xyz;
}
