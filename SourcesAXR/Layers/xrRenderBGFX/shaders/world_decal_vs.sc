$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>



void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0) );
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
    gl_Position.z -= (0.02 / max(gl_Position.w, 0.01) ) + 0.002;
    v_texcoord0 = a_texcoord0;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // Decals are AXR's deffer_base_aref_flat / deffer_model_flat_d pair; the
    // normal and the vertex hemi come from the same place as the static path
    // (deffer_base_flat.vs:13-15, deffer_base_flat.ps:37).
    vec3 Nw = normalize(unpack_bx2(a_normal.xyz));
    v_viewNormal = mul((mat3)u_modelView, Nw);
}
