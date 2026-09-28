$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_bones[255];

void main()
{
    // skin.h:107  int mid = v.N.w * 255 + 0.3;
    // a_normal.w is the alpha byte of the vertHW_1W D3DCOLOR, which FSkinned
    // fills with the pre-multiplied bone slot (FSkinned.cpp:83).
    int i = int(a_normal.w * 255.0 + 0.3);
    // skin.h:52-54  float4 u_position(float4 v) { return float4(v.xyz, 1.f); }
    vec4 p = vec4(a_position.xyz, 1.0);
    // skin.h:71-81 skinning_pos()
    vec3 sk = vec3(
        dot(u_bones[i + 0], p),
        dot(u_bones[i + 1], p),
        dot(u_bones[i + 2], p));
    vec4 viewPos = mul(u_modelView, vec4(sk, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(sk, 1.0));
    v_texcoord0 = a_texcoord0;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // The normal follows the same path as the position in the reference:
    //   skin.h:86     v.N = v.N.zyx;      // a D3DCOLOR's memory bytes are
    //   skin.h:104    v.N.xyz = v.N.zyx;  // B, G, R, A (color_rgba ->
    //                                       // color_argb, xrCore/_color.h:5-6),
    //                                       // so the attribute is (Nz, Ny, Nx)
    //   skin.h:63-69  float3 U = unpack_normal(dir);  // == unpack_bx2, 2*v-1
    //   skin.h:115    o.N = skinning_dir(v.N, m0, m1, m2);
    //   deffer_model_flat.vs:13  O.N = mul((float3x3)m_WV, (float3)I.N);
    // so the object normal is unpacked, rotated by the bone rows and only then
    // taken to eye space. Nothing is normalized in the vertex stage - the
    // reference normalizes in the pixel stage (deffer_base_flat.ps:44
    // normalize((float3)I.N.xyz)), and so does skin_ps.sc.
    vec3 U = unpack_bx2(a_normal.xyz.zyx);
    v_viewNormal = mul((mat3)u_modelView, vec3(
        dot(u_bones[i + 0].xyz, U),
        dot(u_bones[i + 1].xyz, U),
        dot(u_bones[i + 2].xyz, U)));
}
