// Rain / snow streak vertex stage, 1:1 with the AXR R4 forward rain pass.
//
// Reference: dxRainRender.cpp:26  SH_Rain.create("effects\rain", "fx\fx_rain")
// (:46 "effects\snow" for winter) resolves through the shader script
// game_unpacked\shaders\r3\effects_rain.s:1-11, whose `begin` names
// vs = stub_default.vs / ps = stub_default.ps, and stub_default.vs:10-12:
//   O.HPos  = mul( m_WVP, I.P );
//   O.Tex0  = I.Tex0;
//   O.Color = I.Color.bgra;   // swizzle vertex colour
//
// The streaks are a FORWARD draw, not a G-buffer writer: the reference renders
// them into rt_Color, the already-composed LDR image, after the combine
// (r4_rendertarget_phase_combine.cpp:355-366:
//   u_setrt(rt_Generic_0, 0, 0, HW.pBaseZB);
//   g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts)
// with the world xform set to identity (dxRainRender.cpp:265), so m_WVP there is
// exactly m_vp. bgfxRainRender::Render submits an identity model matrix, which
// makes u_modelViewProj the same product.
//
// I.Color.bgra is the D3DCOLOR (0xAARRGGBB) swizzle of the reference. The port
// applies it on the CPU instead: bgfxRainRender.cpp PackColor() reorders ARGB
// into the R,G,B,A memory order COLOR0 is declared with (BGFX_ATTRIB_TYPE_UINT8
// normalised), so v_color0 arrives in the channel order the reference's .bgra
// produces - the same convention every other class in the port follows.
$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0
#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
}
