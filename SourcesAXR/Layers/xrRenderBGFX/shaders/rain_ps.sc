// Rain / snow streak pixel stage, 1:1 with the AXR R4 forward rain pass.
//
// Reference: game_unpacked\shaders\r3\effects_rain.s:1-11 names
// ps = stub_default.ps, and stub_default.ps:5-8:
//   float4 main ( p_TL I ) : SV_Target
//   {
//       return s_base.Sample( smp_base, I.Tex0 )*I.Color;
//   }
// with s_base = t_base = "fx\fx_rain" (dxRainRender.cpp:26) or "fx\fx_snow"
// (:46). The whole streak is the rain texture modulated by the vertex colour,
// which dxRainRender.cpp:113-114 fills with the weather's rain_color and the
// factor_visual density, and which drives the alpha the srcalpha blend uses.
//
// The blend state is NOT part of this shader: effects_rain.s:3-6 gives the pass
// zb(true,false) + blend(srcalpha,invsrcalpha) + aref(true,0), which
// dxRainRender.cpp:264-269 turns into CULL_NONE + a single vertex-coloured quad
// draw. bgfxRainRender::SubmitQuads sets the matching bgfx state.
//
// Single output, unlike particle_ps.sc: a streak is a lit forward quad, it must
// not write the G-buffer's position / packed-normal attachments.
$input v_color0, v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_base, 0);

void main()
{
    gl_FragColor = texture2D(s_base, v_texcoord0) * v_color0;
}
