$input a_position, a_normal, a_texcoord0, a_texcoord2
$output v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_bones[255];

void main()
{
    // skin.h:139  float w = v.N.w;  - the alpha byte of the vertHW_2W
    // D3DCOLOR (FSkinned.cpp:131).
    float w = a_normal.w;
    // skin.h:129,133  int id_0 = v.tc.z;  int id_1 = v.tc.w;  - the bone slots
    // ride in the z/w of the texcoord as plain integers (FSkinned.cpp:136-137).
    int i0 = int(a_texcoord2.x);
    int i1 = int(a_texcoord2.y);
    // skin.h:140-144: the two bone matrices are lerped row by row and the
    // blended rows are then used for both the position and the normal.
    vec4 m0 = u_bones[i0 + 0] + (u_bones[i1 + 0] - u_bones[i0 + 0]) * w;
    vec4 m1 = u_bones[i0 + 1] + (u_bones[i1 + 1] - u_bones[i0 + 1]) * w;
    vec4 m2 = u_bones[i0 + 2] + (u_bones[i1 + 2] - u_bones[i0 + 2]) * w;
    // skin.h:52-54 u_position()
    vec4 p = vec4(a_position.xyz, 1.0);
    // skin.h:71-81 skinning_pos()
    vec3 sk = vec3(dot(m0, p), dot(m1, p), dot(m2, p));
    vec4 viewPos = mul(u_modelView, vec4(sk, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(sk, 1.0));
    v_texcoord0 = a_texcoord0;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // skin.h:122  v.N.xyz = v.N.zyx; (D3DCOLOR bytes are B, G, R, A) and
    // skin.h:141  o.N = skinning_dir(v.N, m0, m1, m2) - the unpacked normal
    // goes through the same blended rows, deffer_model_flat.vs:13 turns the
    // result into the eye normal and deffer_base_flat.ps:44 normalizes it.
    vec3 U = unpack_bx2(a_normal.xyz.zyx);
    v_viewNormal = mul((mat3)u_modelView, vec3(dot(m0.xyz, U), dot(m1.xyz, U), dot(m2.xyz, U)));
}
