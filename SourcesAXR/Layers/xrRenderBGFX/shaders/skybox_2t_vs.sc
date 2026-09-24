$input a_position, a_color0, a_texcoord2
$output v_color0, v_dir, v_worldPos
#include <bgfx_shader.sh>

void main()
{
    vec4 tpos = vec4(a_position * 1000.0, 1.0);
    gl_Position = mul(u_modelViewProj, tpos);
    gl_Position.z = gl_Position.w;
    v_color0 = a_color0;
    v_dir = a_texcoord2.xyz;
    v_worldPos = mul(u_model[0], tpos).xyz;
}
