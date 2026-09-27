$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>



void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0) );
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
    v_texcoord0 = a_texcoord0;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // Eye-space normal for the packed-normal half of the G-buffer attachment.
    // Reference: deffer_base_flat.vs:13-15
    //   I.Nh = unpack_D3DCOLOR(I.Nh);
    //   O.N  = mul( (float3x3)m_WV, unpack_bx2(I.Nh) );
    // a_normal carries the D3DCOLOR bytes verbatim from the source vertex
    // declaration (bgfxRenderCompat.cpp repacks them unchanged), so only
    // unpack_bx2() is left to do. Its .w is the reference hemi I.Nh.w
    // (deffer_base_flat.ps:41 h = I.position.w) and rides along unused: the
    // writers take the hemi per pixel from the normal instead - see gbuf_pack.h
    // for why the vertex/lightmap sources are not reachable yet.
    vec3 Nw = normalize(unpack_bx2(a_normal.xyz));
    v_viewNormal = mul((mat3)u_modelView, Nw);
}
