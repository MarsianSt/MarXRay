$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0, a_texcoord2
$output v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_bones[255];

void main()
{
    // skin.h:225-228: w0/w1/w2 from the NORMAL, TANGENT and BINORMAL alpha
    // bytes, the fourth weight is the remainder (FSkinned.cpp:270-272).
    float w0 = a_normal.w;
    float w1 = a_tangent.w;
    float w2 = a_bitangent.w;
    float w3 = 1.0 - w0 - w1 - w2;
    // skin.h:209  v.ind.xyz = v.ind.zyx;  then :217  id[i] = v.ind[i]*255+0.3.
    // The four indices are packed as one D3DCOLOR (FSkinned.cpp:275
    // color_rgba(index0..3)), whose memory bytes are B, G, R, A, so the
    // attribute reads (index3, index2, index1, index0) and the swizzle
    // reorders it to (index1, index2, index3, index0) - which is the order
    // the .zyx/.y/.x/.w selection below reproduces.
    int i0 = int(a_texcoord2.z * 255.0 + 0.3);
    int i1 = int(a_texcoord2.y * 255.0 + 0.3);
    int i2 = int(a_texcoord2.x * 255.0 + 0.3);
    int i3 = int(a_texcoord2.w * 255.0 + 0.3);
    // skin.h:230-240: the four bone matrices are accumulated row by row with
    // the same weights, then used for position and normal alike.
    vec4 m0 = u_bones[i0 + 0] * w0 + u_bones[i1 + 0] * w1 + u_bones[i2 + 0] * w2 + u_bones[i3 + 0] * w3;
    vec4 m1 = u_bones[i0 + 1] * w0 + u_bones[i1 + 1] * w1 + u_bones[i2 + 1] * w2 + u_bones[i3 + 1] * w3;
    vec4 m2 = u_bones[i0 + 2] * w0 + u_bones[i1 + 2] * w1 + u_bones[i2 + 2] * w2 + u_bones[i3 + 2] * w3;
    // skin.h:52-54 u_position()
    vec4 p = vec4(a_position.xyz, 1.0);
    // skin.h:71-81 skinning_pos()
    vec3 sk = vec3(dot(m0, p), dot(m1, p), dot(m2, p));
    vec4 viewPos = mul(u_modelView, vec4(sk, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(sk, 1.0));
    v_texcoord0 = a_texcoord0;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // skin.h:207  v.N.xyz = v.N.zyx; (D3DCOLOR bytes are B, G, R, A) and
    // skin.h:245  o.N = skinning_dir(v.N, m0, m1, m2) - the unpacked normal
    // goes through the same accumulated rows, deffer_model_flat.vs:13 turns the
    // result into the eye normal and deffer_base_flat.ps:44 normalizes it.
    vec3 U = unpack_bx2(a_normal.xyz.zyx);
    v_viewNormal = mul((mat3)u_modelView, vec3(dot(m0.xyz, U), dot(m1.xyz, U), dot(m2.xyz, U)));
}
