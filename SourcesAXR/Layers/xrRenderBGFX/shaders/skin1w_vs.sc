$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_fogDepth
#include <bgfx_shader.sh>

uniform vec4 u_bones[255];

void main()
{
    int i = int(a_normal.w * 255.0 + 0.5);
    vec4 p = vec4(a_position, 1.0);
    vec3 sk = vec3(
        dot(u_bones[i + 0], p),
        dot(u_bones[i + 1], p),
        dot(u_bones[i + 2], p));
    vec4 viewPos = mul(u_modelView, vec4(sk, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(sk, 1.0));
    v_texcoord0 = a_texcoord0;
    v_fogDepth = length(viewPos.xyz);
}
