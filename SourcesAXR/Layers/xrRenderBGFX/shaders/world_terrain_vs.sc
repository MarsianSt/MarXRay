$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_viewPos, v_viewNormal
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
}
