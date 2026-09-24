$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0, a_texcoord2
$output v_texcoord0, v_fogDepth
#include <bgfx_shader.sh>

uniform vec4 u_bones[255];

void main()
{
    float w0 = a_normal.w;
    float w1 = a_tangent.w;
    float w2 = a_bitangent.w;
    int i0 = int(a_texcoord2.z * 255.0 + 0.5);
    int i1 = int(a_texcoord2.y * 255.0 + 0.5);
    int i2 = int(a_texcoord2.x * 255.0 + 0.5);
    int i3 = int(a_texcoord2.w * 255.0 + 0.5);
    vec4 p = vec4(a_position, 1.0);
    vec3 p0 = vec3(
        dot(u_bones[i0 + 0], p),
        dot(u_bones[i0 + 1], p),
        dot(u_bones[i0 + 2], p));
    vec3 p1 = vec3(
        dot(u_bones[i1 + 0], p),
        dot(u_bones[i1 + 1], p),
        dot(u_bones[i1 + 2], p));
    vec3 p2 = vec3(
        dot(u_bones[i2 + 0], p),
        dot(u_bones[i2 + 1], p),
        dot(u_bones[i2 + 2], p));
    vec3 p3 = vec3(
        dot(u_bones[i3 + 0], p),
        dot(u_bones[i3 + 1], p),
        dot(u_bones[i3 + 2], p));
    vec3 sk = p0 * w0 + p1 * w1 + p2 * w2 + p3 * (1.0 - w0 - w1 - w2);
    vec4 viewPos = mul(u_modelView, vec4(sk, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(sk, 1.0));
    v_texcoord0 = a_texcoord0;
    v_fogDepth = length(viewPos.xyz);
}
