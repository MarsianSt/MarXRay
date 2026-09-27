$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_texcoord1, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>



void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0) );
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
    v_texcoord0 = a_texcoord0;
    // Lightmap uv, the AXR p_flat::lmh (deffer_base_flat.vs:22
    // O.lmh = I.tc1, i.e. TEXCOORD1 passed straight through - the CPU repack has
    // already applied unpack_tc_lmap = /32768, common_functions.h:112). Only the
    // buffers that declare TEXCOORD1 feed this slot (bgfxRenderCompat.cpp
    // s_worldTerrainLayoutDesc), and the pixel stage only reads it when a
    // lightmap is actually bound for the draw.
    v_texcoord1 = a_texcoord1;
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
