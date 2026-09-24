$input a_position, a_normal, a_texcoord0, a_texcoord2
$output v_texcoord0, v_fogDepth
#include <bgfx_shader.sh>

uniform vec4 u_bones[255];

void main()
{
    float w = a_normal.w;
    int i0 = int(a_texcoord2.x + 0.5);
    int i1 = int(a_texcoord2.y + 0.5);
    vec4 p = vec4(a_position, 1.0);
    vec3 p0 = vec3(
        dot(u_bones[i0 + 0], p),
        dot(u_bones[i0 + 1], p),
        dot(u_bones[i0 + 2], p));
    vec3 p1 = vec3(
        dot(u_bones[i1 + 0], p),
        dot(u_bones[i1 + 1], p),
        dot(u_bones[i1 + 2], p));
    vec3 sk = mix(p0, p1, w);
    vec4 viewPos = mul(u_modelView, vec4(sk, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(sk, 1.0));
    v_texcoord0 = a_texcoord0;
    v_fogDepth = length(viewPos.xyz);
}
