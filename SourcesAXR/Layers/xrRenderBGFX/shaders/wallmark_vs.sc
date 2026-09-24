$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    // Wallmarks are coplanar with the surface; bias them towards the camera
    // (same trick as world_decal_vs) so they win the depth test against the
    // base geometry without z-fighting.
    gl_Position.z -= (0.02 / max(gl_Position.w, 0.01)) + 0.002;
    v_color0    = a_color0;
    v_texcoord0 = a_texcoord0;
}
