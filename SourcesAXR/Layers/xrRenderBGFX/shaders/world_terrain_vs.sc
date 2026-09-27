$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_viewPos, v_viewNormal, v_hemi
#include <bgfx_shader.sh>
#include <gbuf_pack.h>



void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0) );
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // Terrain is flat shaded in the reference too: deffer_terrain_mid_flat.ps:56
    // packs I.N straight into the G-buffer, and the bumped variants
    // (deffer_impl_flat.ps) only rotate it by the TBN the port does not bind
    // yet, so the geometric normal is the same value.
    vec3 Nw = normalize(unpack_bx2(a_normal.xyz));
    v_viewNormal = mul((mat3)u_modelView, Nw);
    // Hemi, the reference I.Nh.w the terrain class takes per vertex:
    //   deffer_terrain_flat_d.vs:19  float hemi = I.Nh.w;
    //   deffer_terrain_flat_d.vs:26  O.position = float4(Pe, hemi);
    //   deffer_terrain_mid_flat.ps:53 float4 Ne = float4(normalize(N), D.w);
    // The reference reads it before the unpack_D3DCOLOR/unpack_bx4 of line 39-41
    // below, but unpack_D3DCOLOR is a bgra swizzle (common_functions.h:114) so .w
    // is the same byte either way, and a_normal arrives as 4xUINT8 normalized
    // (bgfxRenderCompat.cpp:1185), i.e. already in 0..1.
    v_hemi = a_normal.w;
}
