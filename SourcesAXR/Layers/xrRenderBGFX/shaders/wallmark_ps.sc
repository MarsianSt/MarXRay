$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_wallmark, 0);

// 1:1 with the reference wallmark pair (r3/effects_wallmarkmult.s +
// r3/simple.ps), not with deffer_base_aref_flat.ps:
//
//   shader:begin ("wmark","simple")
//     : blend  (true, blend.destcolor, blend.srccolor)
//     : aref   (true, 0)
//     : zb     (true, false)          depth test on, depth write off
//     : fog    (false)
//     : wmark  (true)
//     : dx10color_write_enable(true, true, true, false)   RGB written, alpha not
//
// simple.ps is a bare texture fetch - no lighting, no fog, and it does NOT read
// the vertex colour, so the TTL alpha the wallmark engine writes into
// WallmarksEngine.cpp:103 color_rgba(128,128,128,aC) is unused in the reference
// and unused here. The fade in the reference comes from the texture itself: the
// mark textures are authored on a neutral 0.5 grey, and DestColor/SrcColor turns
// that into result = 2*src*dst = dst, i.e. a no-op outside the mark shape.
//
// One output only, because CRenderTarget::phase_wallmarks (r4_rendertarget_phase_combine.cpp:794-799)
// unbind RT slots 1 and 2 and bind the albedo (rt_Color == r2_RT_albedo,
// r4_rendertarget.cpp:455) as slot 0 - the reference pixel stage writes a single
// float4, so the multiply lands on the albedo alone. The position and packed
// G-buffer of the underlying surface must survive untouched, otherwise the mark
// scales P and the packed normal and the deferred lighting and the height fog
// both read corrupted values.
//
// The discard is the aref(true, 0) state of the material (a fixed-function alpha
// test with ref 0 in the reference, world_solid_ps.sc:31 spells the same test for
// the world aref).
void main()
{
    vec4 D = texture2D(s_wallmark, v_texcoord0);
    if (D.w - 0.0 < 0.0)
        discard;
    gl_FragColor = D;
}
