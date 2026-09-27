$input a_position, a_texcoord0
$output v_texcoord0, v_viewPos
#include <bgfx_shader.sh>



void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0) );
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
    gl_Position.z -= (0.02 / max(gl_Position.w, 0.01) ) + 0.002;
    v_texcoord0 = a_texcoord0;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
}
