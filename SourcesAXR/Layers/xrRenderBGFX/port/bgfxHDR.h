#pragma once

#include "../bgfx_capi.h"

namespace bgfxHDR
{
    const bgfx_view_id_t kSceneView = 0;
    const bgfx_view_id_t kCombineView = 2;
    const bgfx_view_id_t kSceneFxView = 6;
    const bgfx_view_id_t kLuminance64View = 7;
    const bgfx_view_id_t kLuminance8View = 8;
    const bgfx_view_id_t kLuminance1View = 9;
    const bgfx_view_id_t kBloomBuildView = 10;
    const bgfx_view_id_t kBloomBlurHView = 11;
    const bgfx_view_id_t kBloomBlurVView = 12;
    const bgfx_view_id_t kGbufDebugView = 13;
    // SMAA (AXR blender_smaa + rendertarget_phase_smaa.cpp, CryRay port of
    // iryoku SMAA ULTRA, color edge detection): edge -> blend weights ->
    // neighbourhood resolve. Runs on the combine LDR output, resolves into
    // the backbuffer before the UI views.
    const bgfx_view_id_t kSmaaEdgeView = 14;
    const bgfx_view_id_t kSmaaBlendView = 15;
    const bgfx_view_id_t kSmaaResolveView = 16;
    // Fog scattering (AXR combine_2_naa.ps:106-121, G_FOG_USE_SCATTERING): a
    // blurred LDR copy feeds the scatter blend. Views run after the combine
    // and before SMAA: downsample -> blur H/V -> scatter resolve.
    const bgfx_view_id_t kFogBlurBuildView = 17;
    const bgfx_view_id_t kFogBlurHView = 18;
    const bgfx_view_id_t kFogBlurVView = 19;
    const bgfx_view_id_t kFogScatterView = 20;
    // Deferred lighting resolve: the only place that turns the unlit albedo of
    // attachment 0 into a lit HDR image (AXR accum_sun.ps + hmodel(), summed by
    // combine_1.ps:114-166). It has to sit after the scene FX view (kSceneFxView,
    // the last writer into the G-buffer) and before the luminance chain, whose
    // middle-grey measurement has to see the lit image. Writes its own
    // full-res RGBA16F target: attachment 0 cannot be read and written in the
    // same pass.
    // bgfx renders the views in ascending id order (bgfx_p.h:3452
    // collectUsedViews), so the SSAO pass that feeds it had to take a free id
    // below this one, and the resolve and the high channel moved down by one.
    const bgfx_view_id_t kResolveView = 22;
    // Split-HDR high channel (AXR r2_RT_generic1, D3DFMT_A8R8G8B8,
    // r4_rendertarget.cpp:490): the /9 encoding of the pre-tonemap image that
    // tonemap() writes as its second output (common_functions.h:32, sky2.ps:60,
    // combine_1.ps:213). Sits after the lighting resolve, which produces the
    // pre-tonemap image, and before the bloom bright pass, the only reader
    // (blender_bloom_build.cpp:18).
    const bgfx_view_id_t kHighView = 23;
    // Sun shadow map (AXR r2_RT_smap_depth, r4_rendertarget.cpp:636). The one
    // writer is the caster pass, which has to run before the scene view, so it
    // takes the lowest free id below kSceneView's block; the reader is the
    // lighting resolve, which therefore also sees a map of the current frame.
    const bgfx_view_id_t kShadowView = 1;
    // Screen-space ambient occlusion (AXR r2_RT_ssao_temp, r4_rendertarget.cpp:861,
    // written by CRenderTarget::phase_ssao, r4_rendertarget_phase_ssao.cpp:12-105).
    // The last id before the lighting resolve: it reads the G-buffer the scene
    // views write (ids 0 and 6) and is read by the resolve, which applies the
    // factor where the reference combine does (combine_1.ps:183).
    const bgfx_view_id_t kSsaoView = 21;

    // Binds the shadow-map view, its sun transform and its clear, then lets the
    // caller re-walk the world with bgfxWorldShadowPass() on. Returns false when
    // the target or the program is unavailable, in which case the world walk is
    // left in its normal mode and the resolve falls back to s = 1. ShadowEnd()
    // clears the flag again and is a no-op when ShadowBegin() returned false.
    bool ShadowBegin();
    void ShadowEnd();
    // 1 between ShadowBegin() and ShadowEnd(). The world walk reads it to swap the
    // world program, the view and the state for the shadow caster ones
    // (bgfxRenderCompat.cpp, pass 0 of bgfxRenderWorld).
    bool ShadowPassActive();
    // The caster program (shadow_vs.sc / shadow_ps.sc), invalid when the shadow
    // targets or the program could not be built.
    bgfx_program_handle_t GetShadowProgram();

    bool CreateHDRTarget(uint16_t _width, uint16_t _height);
    void DestroyHDRTarget();
    bool RecreateOnResize(uint16_t _width, uint16_t _height);
    bool IsReady();
    bool BindScene();
    // Stage-2 lighting resolve. Reads the G-buffer the scene views wrote and
    // fills the lit HDR target. Returns false (drawing nothing) when the program
    // or the target is unavailable; GetLitTexture() then hands out the raw
    // unlit attachment 0, so the frame stays intact exactly as it was before
    // this pass existed.
    bool ResolvePass();
    // Screen-space ambient occlusion, 1:1 with CRenderTarget::phase_ssao
    // (r4_rendertarget_phase_ssao.cpp:12-105) driving ssao_calc_nomsaa: the
    // half-resolution occlusion buffer, computed out of the same G-buffer the
    // resolve reads. Has to run after the scene FX view and before ResolvePass.
    // Returns false (drawing nothing) when its target, its dither texture or its
    // program is unavailable; GetSsaoTexture() then hands out a 1x1 texture
    // holding the neutral 1.0, so the frame stays intact exactly as it was
    // before this pass existed.
    bool SSAOPass();
    // Split-HDR high channel: high = high(tonemap(fog(lit), tm_scale)), i.e. the
    // AXR /9 encoding of the pre-tonemap image. Returns false (drawing nothing)
    // when the target or the program is unavailable; BloomPass then skips instead
    // of reading an unscaled image, so the frame stays intact.
    bool HighPass();
    // The high channel, or an invalid handle when the pass is unavailable.
    bgfx_texture_handle_t GetHighTexture();
    // Publishes the luminance result of this frame: the swap of r2_RT_luminance_cur /
    // _dest (r4_rendertarget_phase_combine.cpp:678) at the end of the frame. The
    // reference swaps there, so the sky, the high pass and the combine all read the
    // previous frame's tm_scale; this is the bgfx equivalent of that swap point.
    void EndFrameLuminance();
    // The HDR image the post chain has to sample: the lit target when the
    // resolve ran this frame, the unlit attachment 0 otherwise.
    bgfx_texture_handle_t GetLitTexture();
    bool LuminancePass();
    bool BloomPass();
    bool CombinePass(uint16_t _width, uint16_t _height);
    // SMAA resolve of the combine output. Returns false (drawing nothing)
    // when SMAA is unavailable; CombinePass then targets the backbuffer
    // directly, so the frame stays intact either way.
    bool SMAAPass(uint16_t _width, uint16_t _height);
    // Fog-scatter chain (see above): builds the blurred LDR copy and blends
    // it over the combine output. Returns false when unavailable; SMAAPass
    // then reads the plain combine output instead, so the frame stays intact.
    bool FogScatterPass(uint16_t _width, uint16_t _height);
    // Stage-1 G-buffer inspector: draws one of the four attachment views over
    // the combine result. Returns false when the inspector is disabled, which is
    // the default (no env var set), so the frame is untouched.
    bool GbufDebugPass(uint16_t _width, uint16_t _height);
    bgfx_texture_handle_t GetTonemapTexture();
    bgfx_texture_handle_t GetBloomTexture();
    // Position G-buffer (Anomaly gbuf position): view-space position written by
    // every world/particle/wallmark PS into attachment 1 of the scene FB.
    bgfx_texture_handle_t GetPositionTexture();
    // Packed G-buffer (Anomaly f_deffer::position, gbuffer_stage.h:7):
    // [gbuf_pack_normal(N).xy, view-space z, hemi] in attachment 2.
    bgfx_texture_handle_t GetGbufTexture();
}
