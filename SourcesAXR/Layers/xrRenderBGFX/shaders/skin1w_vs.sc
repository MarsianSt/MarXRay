$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

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
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // a_normal is the D3DCOLOR of the vertHW_* NORMAL semantic
    // (FSkinned.cpp:61 / :109 / :166 / :246); its xyz is the bx2 packed normal
    // and its w carries the bone index / skin weight, never the hemi. The
    // reference does not rotate the model normal by the skinning bones either -
    // deffer_model_flat.vs:12 O.N = mul((float3x3)m_WV, (float3)I.N).
    vec3 Nw = normalize(unpack_bx2(a_normal.xyz));
    v_viewNormal = mul((mat3)u_modelView, Nw);
}
