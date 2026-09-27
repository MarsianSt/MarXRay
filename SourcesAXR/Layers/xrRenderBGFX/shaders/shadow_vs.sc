$input a_position
$output v_texcoord0
#include <bgfx_shader.sh>

// Sun shadow-map depth pass, the bgfx twin of the AXR sun caster pass
// (game_unpacked/shaders/r3/shadow_direct_base.vs:15):
//
//   v2p_shadow_direct O;  O.hpos = mul(m_WVP, I.P);   return O;
//
// AXR feeds it xform_world = identity, xform_view = identity and
// xform_project = fuckingsun->X.D.combine (archive_sourse/Layers/xrRenderPC_R4/
// r2_R_sun.cpp:729-731), i.e. m_WVP is the sun matrix alone. bgfx splits that
// pair into the view transform, so the port sets the identity view and
// fuckingsun->X.D.combine as the projection (bgfxHDR::ShadowPass) and
// u_modelViewProj collapses to the same product. The per-object model matrix
// (FTreeVisual::GetTreeXform, MT_TREE_ST/MT_TREE_PM) arrives through bgfx's own
// instance transform, which is the AXR xform_world slot.
//
// O.depth (shadow_direct_base.vs:17, the non-HW branch) is not written here:
// the port has no hardware depth-buffer comparison sampler, so the depth travels
// through gl_FragCoord.z in shadow_ps.sc, which is the rasteriser's own
// interpolation of hpos.z/hpos.w - the same value a D24X8/D32F shadow map would
// have stored. v_texcoord0 is an unused carrier: bgfx rejects a program whose
// vertex outputs and fragment inputs differ (bgfx_p.h:6218), so a VS with no
// varying at all cannot be paired with a PS (see varying.def.sc:18-20).
void main()
{
    gl_Position  = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord0  = vec2(0.0, 0.0);
}
