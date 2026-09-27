$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>



void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0) );
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
    gl_Position.z -= (0.02 / max(gl_Position.w, 0.01) ) + 0.002;
    v_texcoord0 = a_texcoord0;
    // Carried for the shared pixel stage (world_solid_ps.sc, paired with this
    // vertex shader at bgfxWorldProgram.cpp:82-83). A decal material is never a
    // lightmap material - uber_deffer.cpp:21-24 only accepts a third texture
    // whose name starts with "lmap" - so u_lmapValid stays 0 for these draws
    // and the varying is not read.
    v_texcoord1 = a_texcoord1;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // Decals are AXR's deffer_base_aref_flat / deffer_model_flat_d pair; the
    // normal and the vertex hemi come from the same place as the static path
    // (deffer_base_flat.vs:13-15, deffer_base_flat.ps:37).
    vec3 Nw = normalize(unpack_bx2(a_normal.xyz));
    v_viewNormal = mul((mat3)u_modelView, Nw);
}
