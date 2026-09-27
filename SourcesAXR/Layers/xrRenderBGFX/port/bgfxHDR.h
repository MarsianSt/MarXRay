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

    bool CreateHDRTarget(uint16_t _width, uint16_t _height);
    void DestroyHDRTarget();
    bool RecreateOnResize(uint16_t _width, uint16_t _height);
    bool IsReady();
    bool BindScene();
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
}
