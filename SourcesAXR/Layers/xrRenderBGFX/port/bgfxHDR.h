#pragma once

#include "../bgfx_capi.h"

namespace bgfxHDR
{
    // View ids. bgfx renders the views in the order the remap table in
    // bgfxRenderDeviceRender::Begin gives it, i.e. the order[] array, not the
    // numeric id: bgfx_set_viewOrder copies the table verbatim into
    // m_viewRemap[0..count-1] (bgfx_p.h:7197 setViewOrder), so entry k is the
    // view that occupies slot k and every id in [0, count) has to appear
    // exactly once - a missing id leaves two views sharing a sort bucket and the
    // second one renders with the first one's framebuffer and rect.
    //
    // The ids are numbered in reference frame order and order[] is the identity,
    // so ascending id IS the frame order and the table below reads as the phase
    // list. Reference order, phase by phase:
    //
    //   casters -> level/lods/Details -> world dynamics -> wallmarks -> rain ->
    //   SSAO -> accum (sun + emissive + point/spot) -> high -> sky + clouds ->
    //   bloom bright pass -> luminance -> gauss H/V -> combine_1 -> forward ->
    //   SMAA -> backbuffer -> gbuf inspector -> UI.
    //
    //   r4_R_render.cpp:392-397 (level, lods, Details), :633 (render_forward's
    //   render_main / r_dsgraph_render_graph(1)) and :538/:546
    //   (r_dsgraph_render_emissive), :472 (phase_wallmarks), :505
    //   (render_rain), :509-521 (sun cascades + accum_direct_blend), :526-547
    //   (phase_accumulator + r_dsgraph_render_emissive), :553-557
    //   (phase_accumulator + render_lights), :574 (phase_combine); inside
    //   r4_rendertarget_accum_direct.cpp:685 the accum family writes
    //   rt_Accumulator, a target of its own; and inside
    //   r4_rendertarget_phase_combine.cpp:143-145 phase_combine clears
    //   rt_Generic_0/1 and binds them, :153-154 CULL_NONE and no stencil,
    //   :166/:170 RenderSky / RenderClouds, :178 the stencil >= 1 mask that keeps
    //   combine_1 to geometry, :299-316 combine_1, :374-388 forward rendering
    //   (RImplementation.render_forward at :386), :393 volumetric, :406
    //   phase_bloom; then r4_rendertarget_phase_bloom.cpp:74-127 (bright pass),
    //   :131 (phase_luminance), :237/:317 (the two gauss passes),
    //   r4_rendertarget_phase_combine.cpp:470-473 (SMAA), :557-657 (combine_2
    //   into the backbuffer) and :678 (the rt_LUM_pool swap).
    //
    // Four placements are the port's rather than the reference's, each forced by
    // the reference's own data flow:
    //   * SSAO has no phase of its own in R4 (r4_rendertarget.h:399-480 lists the
    //     phases and it is not among them) and its factor is applied inside
    //     combine_1 (combine_1.ps:183). It reads the G-buffer, so it sits between
    //     the last G-buffer writer and the resolve.
    //   * The resolve stands in for the whole accum family - accum_direct_blend
    //     (r4_rendertarget_accum_direct.cpp:685, rt_Accumulator), the emissive
    //     pass and the point / spot lights, which the reference sums in
    //     combine_1.ps:114-166. In the reference the accumulator is a target of
    //     its own, so phase_combine can clear rt_Generic_0/1 and draw the sky
    //     over the lit result; here the accumulator IS the lit target, which is
    //     why the sky has to run after the resolve.
    //   * combine_1 is one pass here and needs tm_scale and the bloom texture, so
    //     it runs after the luminance chain and the gauss passes instead of
    //     before them: the measurement is r4_rendertarget_phase_bloom.cpp:131
    //     and the swap that keeps it a frame behind is
    //     r4_rendertarget_phase_combine.cpp:678.
    //   * The forward phase is reserved and empty. Its slot (right after
    //     combine_1) is the reference's; the pass is the mixed and additive
    //     particles, which no pass here draws yet.
    const bgfx_view_id_t kSceneView = 0;
    // Sun shadow casters (AXR r2_RT_smap_depth, r4_rendertarget.cpp:636,
    // r4_R_render.cpp:509-514 render_sun_cascades; the bgfx twin of
    // r2_R_sun.cpp:722-741 phase_smap_direct, submitted as pass 0 of
    // bgfxRenderWorld). It has to be the first view of the frame so the map is
    // already there when the resolve samples it, so order[] puts it in front of
    // the scene view even though its id is above 0. The caster program
    // (shadow_vs.sc / shadow_ps.sc) reads nothing but the vertex position, so
    // nothing in the G-buffer has to exist before it.
    const bgfx_view_id_t kShadowView = 1;
    // World dynamics and emissive geometry: the last writer into the G-buffer.
    // In the reference the priority-1 geometry is drawn by render_forward
    // (r4_R_render.cpp:633 render_main / r_dsgraph_render_graph(1)) and the
    // emissive pass is :538/:546; what lands in the deferred set here is
    // bgfxRenderDynamic and bgfxRenderEnvironmentFx.
    const bgfx_view_id_t kSceneFxView = 2;
    // Rain (AXR render_rain, r4_R_render.cpp:505). It draws rain and
    // thunderbolts, and the reference renders it forward with blending off
    // (dx10RainBlender.cpp:13) straight into rt_Generic_0
    // (r4_rendertarget_draw_rain.cpp), i.e. it is NOT deferred G-buffer
    // geometry. This view still carries the scene G-buffer as its framebuffer -
    // moving the pass to the reference's own forward stage is the rain port's job,
    // this renumbering only gave it a view.
    //
    // DEVIATION, by result and on purpose until the rain gets its own 1:1 task.
    // In R4 the rain is drawn in r4_Render.cpp:502-506 - between phase_wallmarks
    // (:475) and render_sun_cascades (:509) - into rt_Color + rt_Accumulator, i.e.
    // BEFORE the light accumulation, which is why the reference rain is lit by the
    // sun. The rain pass here submits into kForwardView instead, on the
    // {s_smaaInput, s_hdrDepth} target, i.e. onto the already combined and
    // tone-mapped image, so it receives no direct sunlight. Id 4 stays reserved as
    // the rain's slot in order[]; if the rain is moved back to it, order[] has to
    // change with it.
    const bgfx_view_id_t kRainView = 4;
    // Wallmarks (AXR CRenderTarget::phase_wallmarks,
    // r4_rendertarget_phase_combine.cpp:791-804, called from r4_R_render.cpp:472
    // right after the level / lods / Detail passes and before rain, sun,
    // accum_emissive, the lights and the combine). The phase multiplies the marks
    // into the albedo, so it still belongs to the G-buffer pass: after the last
    // G-buffer writer and before SSAO, which measures the occlusion the marks are
    // part of.
    const bgfx_view_id_t kWallmarkView = 3;
    // Screen-space ambient occlusion (AXR r2_RT_ssao_temp, r4_rendertarget.cpp:861,
    // written by CRenderTarget::phase_ssao, r4_rendertarget_phase_ssao.cpp:12-105).
    // Reads the G-buffer the scene views write and is read by the resolve, which
    // applies the factor where the reference combine does (combine_1.ps:183).
    const bgfx_view_id_t kSsaoView = 5;
    // Lighting resolve. See the note at the top: it replaces the reference's
    // rt_Accumulator (r4_rendertarget_accum_direct.cpp:685) and produces the lit
    // image that sky2.ps, the bright pass and combine_1 all sample. It sits after
    // the last G-buffer writer and before the luminance chain, whose middle-grey
    // measurement has to see the lit image. Writes its own full-res RGBA16F
    // target: attachment 0 cannot be read and written in the same pass.
    const bgfx_view_id_t kResolveView = 6;
    // Split-HDR high channel (AXR r2_RT_generic1, D3DFMT_A8R8G8B8,
    // r4_rendertarget.cpp:490): the /9 encoding of the pre-tonemap image that
    // tonemap() writes as its second output (common_functions.h:32, sky2.ps:60,
    // combine_1.ps:213). It has to run before the sky, which writes the high
    // channel itself (sky2.ps:60 does its own /def_hdr), and before the bloom
    // bright pass, the other reader (blender_bloom_build.cpp:18).
    const bgfx_view_id_t kHighView = 7;
    // Sky and clouds (AXR r4_rendertarget_phase_combine.cpp:166 RenderSky and
    // :170 RenderClouds, both after the clear of rt_Generic_0/1 at :143-144, with
    // CULL_NONE at :153 and no stencil at :154). They are drawn into the low and
    // high HDR targets and combine_1 blends them over the lit image by the stencil
    // mask at :178, so the reference slot is after the resolve and after the high
    // channel - which is id 8 below.
    //
    // The port still draws them into G-buffer attachment 0
    // (bgfxEnvironmentRender.cpp), and that forces a different ORDER: the resolve
    // writes the lit image in place, where the reference's accumulator is a target
    // of its own (r4_rendertarget_accum_direct.cpp:685), so a sky drawn after it
    // would be overwritten. order[] therefore keeps kSkyView inside the G-buffer
    // block; the id becomes the reference's again when the sky moves to the low
    // and high targets.
    const bgfx_view_id_t kSkyView = 8;
    // Bloom bright pass (the first block of AXR phase_bloom,
    // r4_rendertarget_phase_bloom.cpp:74-127, driven by blender_bloom_build.cpp,
    // whose s_image is r2_RT_generic1, i.e. the high channel). It has to run
    // after the sky, which is a second writer of that channel, and before the
    // luminance chain, which measures the bright-pass output
    // (blender_luminance.cpp:18) and before the gauss passes overwrite
    // rt_Bloom_1.
    const bgfx_view_id_t kBloomBuildView = 9;
    // Luminance chain (AXR phase_luminance, r4_rendertarget_phase_bloom.cpp:131 ->
    // r4_rendertarget_phase_luminance.cpp:19-143: 1/64 -> 1/8 -> 1/1).
    const bgfx_view_id_t kLuminance64View = 10;
    const bgfx_view_id_t kLuminance8View = 11;
    const bgfx_view_id_t kLuminance1View = 12;
    // The two gauss passes (r4_rendertarget_phase_bloom.cpp:237 horizontal into
    // rt_Bloom_2, :317 vertical back into rt_Bloom_1). They have to run after the
    // luminance chain, which reads the bright-pass output before they overwrite
    // it, and before the combine, the consumer of rt_Bloom_1 (combine_bloom).
    const bgfx_view_id_t kBloomBlurHView = 13;
    const bgfx_view_id_t kBloomBlurVView = 14;
    // combine_1 (AXR r4_rendertarget_phase_combine.cpp:299-316): the light, the
    // fog and the sky blend over the lit target into the LDR output.
    const bgfx_view_id_t kCombineView = 15;
    // Forward rendering (AXR r4_rendertarget_phase_combine.cpp:374-388,
    // RImplementation.render_forward() at :386 -> r4_R_render.cpp:617-640): the
    // geometry that cannot be deferred - here the mixed and additive particles -
    // drawn over the combined, tone-mapped LDR image. The reference puts it after
    // combine_1 and before the volumetric combine and phase_bloom, which is this
    // slot. The view is reserved and the pass is not implemented yet, so it draws
    // nothing.
    const bgfx_view_id_t kForwardView = 16;
    // SMAA (AXR blender_smaa + rendertarget_phase_smaa.cpp, CryRay port of
    // iryoku SMAA ULTRA, color edge detection): edge -> blend weights ->
    // neighbourhood resolve. Runs on the combine LDR output and resolves into
    // the backbuffer, the last thing before the UI views
    // (r4_rendertarget_phase_combine.cpp:470-473, :557-657).
    const bgfx_view_id_t kSmaaEdgeView = 17;
    const bgfx_view_id_t kSmaaBlendView = 18;
    const bgfx_view_id_t kSmaaResolveView = 19;
    // Stage-1 G-buffer inspector: draws one of the four attachment views over
    // the finished frame, so it is the last view of the post chain. The
    // reference has no such pass; it is gated behind XRGBUF_DEBUG and draws
    // nothing by default.
    const bgfx_view_id_t kGbufDebugView = 20;
    // Intro video / second-viewport playback (was the bare 1 in
    // bgfxUISequenceVideoItem.cpp:166). It draws into the backbuffer and has to
    // run after the post chain, so it does not overwrite the finished frame.
    const bgfx_view_id_t kIntroView = 21;
    // Actor hands and weapon (was the bare 3 / kHudViewId,
    // bgfxRenderCompat.cpp:2231). Its own depth clear keeps it off the world.
    const bgfx_view_id_t kHudView = 22;
    // 2D UI, the target of bgfxUISubmitView() (was the bare 4,
    // bgfxRenderDeviceRender.cpp End()).
    const bgfx_view_id_t kUiView = 23;
    // ImGui (was the bare 5, bgfxImGuiRender.cpp:141-233).
    const bgfx_view_id_t kImguiView = 24;

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
    // The wallmark phase target: the albedo attachment plus the scene depth and
    // nothing else, i.e. the bgfx equivalent of phase_wallmarks' set_RT(NULL, 2),
    // set_RT(NULL, 1), u_setrt(rt_Color, NULL, NULL, HW.pBaseZB)
    // (r4_rendertarget_phase_combine.cpp:794-799). Invalid when the HDR target is
    // not up, in which case the wallmark pass draws nothing.
    bgfx_frame_buffer_handle_t GetWallmarkFrameBuffer();
    // Forward phase target: the combine LDR output plus the scene depth and
    // nothing else, i.e. the bgfx twin of
    //   u_setrt(rt_Generic_0, 0, 0, HW.pBaseZB)
    // (r4_rendertarget_phase_combine.cpp:378-380), where the forward geometry
    // lands on the already combined, tone-mapped image with the level depth still
    // bound - the same attachment pair phase_wallmarks uses
    // (r4_rendertarget_phase_combine.cpp:794-799). Invalid when the HDR target or
    // the combine output is down, in which case the forward pass draws nothing.
    bgfx_frame_buffer_handle_t GetForwardFrameBuffer();
}
