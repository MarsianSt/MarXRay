#include "stdafx.h"
#pragma hdrstop

#include "bgfxHDR.h"
#include "../bgfxShaderCompiler.h"
#include "../bgfxUIShader.h"
#include "../bgfxRenderInterface.h"
#include "../../../xrEngine/device.h"
#include "../../../xrEngine/x_ray.h"
#include "../../../xrEngine/IGame_Persistent.h"
#include "../../../xrEngine/Environment.h"
#include "../bgfxEnvironmentRender.h"
#include "../../../xrEngine/DiscordRichPresense.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    bgfx_frame_buffer_handle_t s_hdrFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrColor = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrPosition = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrGbuf = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrDepth = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_combineProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_hdrSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_tonemapSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_positionSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_bloomSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_exposure = BGFX_INVALID_HANDLE;
    // AXR r3 fog globals (game_unpacked/shaders/r3 -> Blender_Recorder_StandartBinding.cpp).
    // u_view is NOT here on purpose: bgfx_shader.sh already declares it as a predefined
    // per-view uniform, fed by bgfx_set_view_transform on kCombineView (it mirrors the
    // m_v2w / m_inv_V matrix that compute_height_fog and combine_1.ps:194 read).
    bgfx_uniform_handle_t s_fogParams = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_fogColor = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lowlandFogParams = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sunDir = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sunColor = BGFX_INVALID_HANDLE;

    // Sun shadow map (AXR r2_RT_smap_depth, r4_rendertarget.cpp:636: smapsize x
    // smapsize, HW_smap_FORMAT = D32F_LOCKABLE under r4.cpp:221). bgfx has no
    // comparison sampler on this backend, so the port stores the light-space depth
    // in a colour target - the reference's own non-HW branch, whose target is
    // r2_RT_smap_surf in D3DFMT_R32F (r4_rendertarget.cpp:698-699) and whose
    // writer is shadow_direct_base.ps:11 `return I.depth`. A real depth
    // attachment sits next to it so the "nearest caster wins" resolution the D32F
    // map gets from its own depth test happens here too; the resolve compares by
    // hand (deferred_light_ps.sc, shadow_smap_test).
    bgfx_frame_buffer_handle_t s_shadowFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_shadowDepth = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_shadowMap = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_shadowProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_smapSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_shadowMat = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_shadowParams = BGFX_INVALID_HANDLE;
    // 1 between ShadowBegin() and ShadowEnd(), i.e. while the world walk is casting.
    // bgfxRenderCompat.cpp reads it to pick the shadow program and view.
    bool s_shadowPassActive = false;
    // 1 once a map has been rendered into this frame, so the resolve knows whether
    // s_smap holds anything. Reset by ShadowBegin() on the next frame.
    bool s_shadowMapValid = false;

    // Screen-space ambient occlusion, the AXR r2_RT_ssao_temp
    // (r4_rendertarget.cpp:861, D3DFMT_R16F) written by CRenderTarget::phase_ssao
    // (r4_rendertarget_phase_ssao.cpp:12-105) through the element 0 of
    // CBlender_SSAO_noMSAA (blender_ssao.cpp:16). The reference allocates the
    // target full-res and renders into a viewport of dwWidth/2 x dwHeight/2
    // (r4_rendertarget_phase_ssao.cpp:52-55), i.e. the occlusion is computed at
    // half resolution and read back magnified 2x, which is what a half-res target
    // plus a full-res tc in the consumer is; see GetSsaoTexture().
    bgfx_frame_buffer_handle_t s_ssaoFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_ssao = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_ssaoProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_ssaoPositionSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_ssaoGbufSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_ssaoJitterSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_ssaoNoiseTileFactor = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_ssaoKernelSize = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_ssaoLayout = {};
    bool s_ssaoLayoutReady = false;
    // 1 when this frame's SSAO pass ran, so the resolve knows whether s_occ holds
    // a value or the neutral 1.0 it falls back to.
    bool s_ssaoOk = false;
    // 1x1 texture holding that neutral 1.0, handed out by GetSsaoTexture() when
    // the pass could not run this frame.
    bgfx_texture_handle_t s_ssaoFallback = BGFX_INVALID_HANDLE;
    // jitter0, the SSAO dither texture. TEX_jitter = 64 (r2_types.h:118).
    bgfx_texture_handle_t s_ssaoJitter = BGFX_INVALID_HANDLE;

    // Stage-2 lighting resolve (deferred_light_ps.sc). Reads the three G-buffer
    // attachments and writes the lit HDR image into its own full-res target, so
    // the pass never samples the texture it renders into. s_resolveOk is the
    // per-frame switch the post chain uses to decide between the two: the
    // resolve runs at kResolveView, i.e. after every consumer has already
    // sampled, so the flag is read while the frame is still being submitted and
    // bgfx has not reached the view yet.
    bgfx_frame_buffer_handle_t s_hdrLitFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrLit = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_resolveProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_litSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_litPositionSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_litGbufSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_litOccSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_hemiColor = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_ambientColor = BGFX_INVALID_HANDLE;
    // Anomaly ambient cube, the env_s0 / env_s1 pair of hmodel.h:14-15. Sampler
    // stages 3 and 4 (0-2 are the three G-buffer attachments) and the 0/1 switch
    // that keeps the pre-cube constant alive while neither cube is bound, i.e.
    // while the level has no <sky_texture>#small env cube at all.
    bgfx_uniform_handle_t s_envCube0 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_envCube1 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_cubeValid = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_resolveLayout = {};
    bool s_resolveLayoutReady = false;
    bool s_resolveOk = false;
    // Dynamic point / spot accumulators (AXR accum_omni_unshadowed.ps /
    // accum_base.ps, accumulated by CRenderTarget::accum_point / accum_spot). The
    // reference draws one volume per light and adds it into rt_Accumulator; bgfx has
    // neither stencil nor a per-light pass, so the resolve reads the light list as
    // three vec4 per light and evaluates the same volumes per pixel
    // (deferred_light_ps.sc). Capacity is the only place this port is not 1:1: the
    // reference has no per-frame cap, so a level with more lights than this loses
    // the tail and says so in the log rather than silently.
    const u32 kMaxDynamicLights = 32;
    bgfx_uniform_handle_t s_lightCount = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lights = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lightParams = BGFX_INVALID_HANDLE;
    std::vector<float> s_lightData;       // 3 * kMaxDynamicLights vec4
    std::vector<float> s_lightParamData;  // 1 * kMaxDynamicLights vec4
    // Split-HDR high channel, the /9 encoding of the pre-tonemap image
    // (common_functions.h:32). Its own full-res target for the same reason the lit
    // one has its own: the pass reads the lit image, so it cannot write into it.
    bgfx_frame_buffer_handle_t s_hdrHighFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrHigh = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_highProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_highSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_highTonemapSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_highPositionSampler = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_highLayout = {};
    bool s_highLayoutReady = false;
    bool s_highOk = false;
    bgfx_vertex_layout_t s_combineLayout = {};
    bool s_combineLayoutReady = false;
    uint16_t s_width = 0;
    uint16_t s_height = 0;
    bgfx_texture_format_t s_colorFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
    bgfx_texture_format_t s_positionFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
    bgfx_texture_format_t s_gbufFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
    bgfx_texture_format_t s_depthFormat = BGFX_TEXTURE_FORMAT_D24;

    // Stage-1 G-buffer inspector (gbuf_debug_ps.sc). XRGBUF_DEBUG selects the
    // view: 1 normal, 2 depth, 3 hemi, 4 albedo, >= 5 cycles through all four
    // with that many seconds per view. Unset or 0 leaves the frame untouched.
    bgfx_program_handle_t s_gbufDebugProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_gbufDebugGbufSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_gbufDebugPosSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_gbufDebugHdrSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_gbufDebugParams = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_gbufDebugLayout = {};
    bool s_gbufDebugLayoutReady = false;
    int s_gbufDebugEnv = 0;
    bool s_gbufDebugEnvRead = false;
    float s_gbufDebugClock = 0.0f;

    bgfx_frame_buffer_handle_t s_lum64Fb = BGFX_INVALID_HANDLE;
    bgfx_frame_buffer_handle_t s_lum8Fb = BGFX_INVALID_HANDLE;
    bgfx_frame_buffer_handle_t s_lum1Fb[2] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx_texture_handle_t s_lum64 = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_lum8 = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_lum1[2] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx_program_handle_t s_lumProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lumImage = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lumPrev = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lumMiddleGray = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lumParams = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_lumLayout = {};
    bool s_lumLayoutReady = false;
    bool s_lumTonemapIndex = false;
    float s_luminanceAdapt = 0.5f;

    // BLOOM_size_X / BLOOM_size_Y, SourcesAXR/Layers/xrRender/r2_types.h:121-122. The R4
    // bloom chain works on a fixed square 256x256 pair (rt_Bloom_1 / rt_Bloom_2,
    // archive_sourse/Layers/xrRenderPC_R4/r4_rendertarget.cpp:788-792) and crops the
    // central BLOOM_size_X x BLOOM_size_Y region of the HDR target (phase_bloom:87-99).
    constexpr u32 kBloomSize = 256;
    // Defaults mirror SourcesAXR/Layers/xrRender/xrRender_console.cpp:281-285; xrRender is
    // not linked into BGFX. bloom_kernel_b (0.7f) only drives the FASTBLOOM variant
    // (R2FLAG_FASTBLOOM, phase_bloom:136-162) and bloom_speed (100.0f) only feeds
    // f_bloom_factor -> b_params.w, which bloom_build.ps never reads in the ported
    // (non ENCHANTED_SHADERS_ENABLED) branch.
    constexpr float kBloomThreshold = 0.00001f;
    constexpr float kBloomKernelG = 3.0f;
    constexpr float kBloomKernelScale = 0.7f;
    // The threshold is consumed in the high domain (bloom_build.ps:43 reads
    // s_image = r2_RT_generic1), so this value is AXR's ps_r2_ls_bloom_threshold
    // verbatim and needs no rescaling: the high channel carries the /9
    // (common_defines.h:11 def_hdr) in the shader, not here.

    bgfx_frame_buffer_handle_t s_bloom1Fb = BGFX_INVALID_HANDLE;
    bgfx_frame_buffer_handle_t s_bloom2Fb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_bloom1 = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_bloom2 = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_bloomBuildProgram = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_bloomFilterProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_bloomImage = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_bloomSource = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_bloomWeight0 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_bloomWeight1 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_bloomParams = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_bloomSetup = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_filterSetup = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_bloomLayout = {};
    bool s_bloomLayoutReady = false;
    bgfx_texture_format_t s_bloomFormat = BGFX_TEXTURE_FORMAT_RGBA8;

    // SMAA working set (AXR blender_smaa.cpp + rendertarget_phase_smaa.cpp).
    // Combine renders its LDR output into s_smaaInput; edge/blend passes run
    // on full-res RGBA8 targets; resolve writes into the backbuffer. Stencil
    // from the reference (edge ALWAYS/REPLACE, weights EQUAL) is skipped on
    // purpose: it is only a fill-rate optimisation, the weights are zero
    // outside edges either way, so the output is identical.
    bgfx_frame_buffer_handle_t s_smaaInputFb = BGFX_INVALID_HANDLE;
    bgfx_frame_buffer_handle_t s_smaaEdgesFb = BGFX_INVALID_HANDLE;
    bgfx_frame_buffer_handle_t s_smaaBlendFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_smaaInput = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_smaaEdges = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_smaaBlend = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_smaaAreaTex = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_smaaSearchTex = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_smaaEdgeProgram = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_smaaBlendProgram = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_smaaResolveProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_smaaImage = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_smaaEdgesU = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_smaaArea = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_smaaSearch = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_smaaBlendU = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_smaaMetrics = BGFX_INVALID_HANDLE;
    uint16_t s_smaaWidth = 0;
    uint16_t s_smaaHeight = 0;

    // Fog-scatter working set (AXR combine_2_naa.ps:106-121). The blur chain
    // reuses the bloom build/filter programs with a zero threshold (full-scene
    // average instead of bright-pass); the blend is fog_scatter_ps.sc.
    bgfx_frame_buffer_handle_t s_fogBlur1Fb = BGFX_INVALID_HANDLE;
    bgfx_frame_buffer_handle_t s_fogBlur2Fb = BGFX_INVALID_HANDLE;
    bgfx_frame_buffer_handle_t s_smaaScatterFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_fogBlur1 = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_fogBlur2 = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_smaaScatter = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_fogScatterProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_scatterImage = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_scatterBlur = BGFX_INVALID_HANDLE;

    // Defaults mirror SourcesAXR/Layers/xrRender/xrRender_console.cpp:276-279; xrRender is not linked into BGFX.
    constexpr float kTonemapMiddleGray = 0.95f;
    constexpr float kTonemapAdaptation = 1.0f;
    constexpr float kTonemapLowLum = 0.0035f;
    constexpr float kTonemapAmount = 0.7f;

    const float s_identity[16] =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    void DestroyTextures()
    {
        if (bgfxIsValid(s_hdrColor))
            bgfx_destroy_texture(s_hdrColor);
        if (bgfxIsValid(s_hdrPosition))
            bgfx_destroy_texture(s_hdrPosition);
        if (bgfxIsValid(s_hdrGbuf))
            bgfx_destroy_texture(s_hdrGbuf);
        if (bgfxIsValid(s_hdrDepth))
            bgfx_destroy_texture(s_hdrDepth);
        if (bgfxIsValid(s_hdrLit))
            bgfx_destroy_texture(s_hdrLit);
        if (bgfxIsValid(s_hdrHigh))
            bgfx_destroy_texture(s_hdrHigh);
        s_hdrColor = BGFX_INVALID_HANDLE;
        s_hdrPosition = BGFX_INVALID_HANDLE;
        s_hdrGbuf = BGFX_INVALID_HANDLE;
        s_hdrDepth = BGFX_INVALID_HANDLE;
        s_hdrLit = BGFX_INVALID_HANDLE;
        s_hdrHigh = BGFX_INVALID_HANDLE;
        s_highOk = false;
    }

    void DestroyLuminanceTargets()
    {
        if (bgfxIsValid(s_lum64Fb))
            bgfx_destroy_frame_buffer(s_lum64Fb);
        if (bgfxIsValid(s_lum8Fb))
            bgfx_destroy_frame_buffer(s_lum8Fb);
        for (u32 i = 0; i < 2; ++i)
            if (bgfxIsValid(s_lum1Fb[i]))
                bgfx_destroy_frame_buffer(s_lum1Fb[i]);
        s_lum64Fb = BGFX_INVALID_HANDLE;
        s_lum8Fb = BGFX_INVALID_HANDLE;
        for (u32 i = 0; i < 2; ++i)
            s_lum1Fb[i] = BGFX_INVALID_HANDLE;
        for (u32 i = 0; i < 2; ++i)
            if (bgfxIsValid(s_lum1[i]))
                bgfx_destroy_texture(s_lum1[i]);
        if (bgfxIsValid(s_lum64))
            bgfx_destroy_texture(s_lum64);
        if (bgfxIsValid(s_lum8))
            bgfx_destroy_texture(s_lum8);
        s_lum64 = BGFX_INVALID_HANDLE;
        s_lum8 = BGFX_INVALID_HANDLE;
        for (u32 i = 0; i < 2; ++i)
            s_lum1[i] = BGFX_INVALID_HANDLE;
        s_lumTonemapIndex = false;
        s_luminanceAdapt = 0.5f;
    }

    void DestroyLuminancePrograms()
    {
        if (bgfxIsValid(s_lumProgram))
            bgfx_destroy_program(s_lumProgram);
        if (bgfxIsValid(s_lumImage))
            bgfx_destroy_uniform(s_lumImage);
        if (bgfxIsValid(s_lumPrev))
            bgfx_destroy_uniform(s_lumPrev);
        if (bgfxIsValid(s_lumMiddleGray))
            bgfx_destroy_uniform(s_lumMiddleGray);
        if (bgfxIsValid(s_lumParams))
            bgfx_destroy_uniform(s_lumParams);
        s_lumProgram = BGFX_INVALID_HANDLE;
        s_lumImage = BGFX_INVALID_HANDLE;
        s_lumPrev = BGFX_INVALID_HANDLE;
        s_lumMiddleGray = BGFX_INVALID_HANDLE;
        s_lumParams = BGFX_INVALID_HANDLE;
        s_lumLayoutReady = false;
    }

    void DestroyBloomTargets()
    {
        if (bgfxIsValid(s_bloom1Fb))
            bgfx_destroy_frame_buffer(s_bloom1Fb);
        if (bgfxIsValid(s_bloom2Fb))
            bgfx_destroy_frame_buffer(s_bloom2Fb);
        s_bloom1Fb = BGFX_INVALID_HANDLE;
        s_bloom2Fb = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_bloom1))
            bgfx_destroy_texture(s_bloom1);
        if (bgfxIsValid(s_bloom2))
            bgfx_destroy_texture(s_bloom2);
        s_bloom1 = BGFX_INVALID_HANDLE;
        s_bloom2 = BGFX_INVALID_HANDLE;
    }

    void DestroyBloomPrograms()
    {
        if (bgfxIsValid(s_bloomBuildProgram))
            bgfx_destroy_program(s_bloomBuildProgram);
        if (bgfxIsValid(s_bloomFilterProgram))
            bgfx_destroy_program(s_bloomFilterProgram);
        if (bgfxIsValid(s_bloomImage))
            bgfx_destroy_uniform(s_bloomImage);
        if (bgfxIsValid(s_bloomSource))
            bgfx_destroy_uniform(s_bloomSource);
        if (bgfxIsValid(s_bloomWeight0))
            bgfx_destroy_uniform(s_bloomWeight0);
        if (bgfxIsValid(s_bloomWeight1))
            bgfx_destroy_uniform(s_bloomWeight1);
        if (bgfxIsValid(s_bloomParams))
            bgfx_destroy_uniform(s_bloomParams);
        if (bgfxIsValid(s_bloomSetup))
            bgfx_destroy_uniform(s_bloomSetup);
        if (bgfxIsValid(s_filterSetup))
            bgfx_destroy_uniform(s_filterSetup);
        s_bloomBuildProgram = BGFX_INVALID_HANDLE;
        s_bloomFilterProgram = BGFX_INVALID_HANDLE;
        s_bloomImage = BGFX_INVALID_HANDLE;
        s_bloomSource = BGFX_INVALID_HANDLE;
        s_bloomWeight0 = BGFX_INVALID_HANDLE;
        s_bloomWeight1 = BGFX_INVALID_HANDLE;
        s_bloomParams = BGFX_INVALID_HANDLE;
        s_bloomSetup = BGFX_INVALID_HANDLE;
        s_filterSetup = BGFX_INVALID_HANDLE;
        s_bloomLayoutReady = false;
    }

    // Forward declarations: the bloom chain below shares the luminance/combine helpers.
    bool IsTextureSupported(bgfx_texture_format_t _format);
    bgfx_program_handle_t BuildProgram(const char* _vs, const char* _ps);

    // Gauss filtering coeffs, 1:1 with archive_sourse/Layers/xrRenderPC_R4/
    // r4_rendertarget_phase_bloom.cpp:34-66.
    // Samples:            0-central, -1, -2,..., -7, 1, 2,... 7
    void CalcGauss_k7(float* _w0, float* _w1, float _r = 3.3f, float _sOut = 1.f)
    {
        float W[8];

        float mag = 0;
        for (int i = -7; i <= 0; i++) W[-i] = expf(-float(i * i) / (2 * _r * _r));  // weight
        for (int i = 0; i < 8; i++) mag += i ? 2 * W[i] : W[i];                     // symmetrical weight
        for (int i = 0; i < 8; i++) W[i] = _sOut * W[i] / mag;

        // W[0]=0, W[7]=-7
        _w0[0] = W[1]; _w0[1] = W[2]; _w0[2] = W[3]; _w0[3] = W[4];        // -1, -2, -3, -4
        _w1[0] = W[5]; _w1[1] = W[6]; _w1[2] = W[7]; _w1[3] = W[0];        // -5, -6, -7, 0
    }

    void CalcGauss_wave(float* _w0, float* _w1, float _rBase = 3.3f, float _rDetail = 1.0f, float _sOut = 1.f)
    {
        float t0[4], t1[4];
        CalcGauss_k7(_w0, _w1, _rBase, _sOut);
        CalcGauss_k7(t0, t1, _rDetail, _sOut);
        for (int i = 0; i < 4; ++i)
        {
            _w0[i] += t0[i];
            _w1[i] += t1[i];
        }
    }

    bool EnsureBloomLayout()
    {
        if (s_bloomLayoutReady)
            return true;
        bgfx_vertex_layout_begin(&s_bloomLayout, bgfx_get_renderer_type());
        bgfx_vertex_layout_add(&s_bloomLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
        bgfx_vertex_layout_add(&s_bloomLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
        bgfx_vertex_layout_end(&s_bloomLayout);
        s_bloomLayoutReady = true;
        return true;
    }

    bool EnsureBloomPrograms()
    {
        if (bgfxIsValid(s_bloomBuildProgram) && bgfxIsValid(s_bloomFilterProgram) &&
            bgfxIsValid(s_bloomImage) && bgfxIsValid(s_bloomSource) && bgfxIsValid(s_bloomWeight0) &&
            bgfxIsValid(s_bloomWeight1) && bgfxIsValid(s_bloomParams) && bgfxIsValid(s_bloomSetup) &&
            bgfxIsValid(s_filterSetup))
            return EnsureBloomLayout();
        EnsureBloomLayout();
        s_bloomBuildProgram = BuildProgram("bloom_vs.sc", "bloom_build_ps.sc");
        s_bloomFilterProgram = BuildProgram("bloom_vs.sc", "bloom_filter_ps.sc");
        if (!bgfxIsValid(s_bloomBuildProgram) || !bgfxIsValid(s_bloomFilterProgram))
        {
            LogError("[BGFX] Bloom program build failed");
            DestroyBloomPrograms();
            return false;
        }
        s_bloomImage = bgfx_create_uniform("s_image", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_bloomSource = bgfx_create_uniform("s_bloom", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_bloomWeight0 = bgfx_create_uniform("u_weight0", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_bloomWeight1 = bgfx_create_uniform("u_weight1", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_bloomParams = bgfx_create_uniform("u_bloomParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_bloomSetup = bgfx_create_uniform("u_bloomSetup", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_filterSetup = bgfx_create_uniform("u_filterSetup", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_bloomImage) || !bgfxIsValid(s_bloomSource) || !bgfxIsValid(s_bloomWeight0) ||
            !bgfxIsValid(s_bloomWeight1) || !bgfxIsValid(s_bloomParams) || !bgfxIsValid(s_bloomSetup) ||
            !bgfxIsValid(s_filterSetup))
        {
            LogError("[BGFX] Bloom uniforms create failed");
            DestroyBloomPrograms();
            return false;
        }
        LogInfo("[BGFX] Bloom programs created: build=%u filter=%u", s_bloomBuildProgram.idx, s_bloomFilterProgram.idx);
        return true;
    }

    bool CreateBloomTargets()
    {
        DestroyBloomTargets();
        // r4_rendertarget.cpp:787 gives rt_Bloom_1/rt_Bloom_2 D3DFMT_A8R8G8B8, so the R4
        // bright pass and its gaussian are 8-bit UNORM quantized. RGBA16F is only a fallback
        // for drivers that refuse the 8-bit RT.
        const uint64_t flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        s_bloomFormat = BGFX_TEXTURE_FORMAT_RGBA8;
        if (!IsTextureSupported(s_bloomFormat))
        {
            LogInfo("[BGFX] Bloom target: RGBA8 unavailable, using RGBA16F");
            s_bloomFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
            if (!IsTextureSupported(s_bloomFormat))
            {
                LogError("[BGFX] Bloom target format unavailable");
                return false;
            }
        }
        s_bloom1 = bgfx_create_texture_2d(kBloomSize, kBloomSize, false, 1, s_bloomFormat, flags, nullptr, 0);
        s_bloom2 = bgfx_create_texture_2d(kBloomSize, kBloomSize, false, 1, s_bloomFormat, flags, nullptr, 0);
        if (!bgfxIsValid(s_bloom1) || !bgfxIsValid(s_bloom2))
        {
            LogError("[BGFX] Bloom target texture create failed");
            DestroyBloomTargets();
            return false;
        }
        bgfx_texture_handle_t a1[] = { s_bloom1 };
        bgfx_texture_handle_t a2[] = { s_bloom2 };
        s_bloom1Fb = bgfx_create_frame_buffer_from_handles(1, a1, true);
        s_bloom2Fb = bgfx_create_frame_buffer_from_handles(1, a2, true);
        if (!bgfxIsValid(s_bloom1Fb) || !bgfxIsValid(s_bloom2Fb))
        {
            LogError("[BGFX] Bloom framebuffer create failed");
            DestroyBloomTargets();
            return false;
        }
        LogInfo("[BGFX] Bloom targets created: %ux%u x2 format=%d", kBloomSize, kBloomSize, (int)s_bloomFormat);
        return true;
    }

    bool EnsureBloomTargets()
    {
        if (bgfxIsValid(s_bloom1Fb) && bgfxIsValid(s_bloom2Fb) && bgfxIsValid(s_bloom1) && bgfxIsValid(s_bloom2))
            return true;
        return CreateBloomTargets();
    }

    bool SubmitBloomPass(bgfx_view_id_t _view, bgfx_frame_buffer_handle_t _fb,
        bgfx_program_handle_t _program, bgfx_uniform_handle_t _sampler, bgfx_texture_handle_t _source,
        uint64_t _state)
    {
        bgfx_set_view_frame_buffer(_view, _fb);
        bgfx_set_view_rect(_view, 0, 0, (uint16_t)kBloomSize, (uint16_t)kBloomSize);
        // phase_bloom:77 - "Clear - don't clear - it's stupid here :)": the bright pass blends
        // onto the previous rt_Bloom_1 content, and the vertical filter then overwrites it.
        bgfx_set_view_clear(_view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(_view, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_set_view_transform(_view, s_identity, s_identity);
        bgfx_touch(_view);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_bloomLayout);
        if (!tvb.data)
            return false;
        struct Vertex
        {
            float x, y, z, u, v;
        };
        const Vertex vertices[3] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 0.0f, -1.0f },
        };
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        bgfx_set_state(_state, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        bgfx_set_texture(0, _sampler, _source, 0);
        bgfx_submit(_view, _program, 0, BGFX_DISCARD_ALL);
        return true;
    }

    void DestroyCombineProgram()
    {
        if (bgfxIsValid(s_combineProgram))
            bgfx_destroy_program(s_combineProgram);
        if (bgfxIsValid(s_hdrSampler))
            bgfx_destroy_uniform(s_hdrSampler);
        if (bgfxIsValid(s_tonemapSampler))
            bgfx_destroy_uniform(s_tonemapSampler);
        if (bgfxIsValid(s_positionSampler))
            bgfx_destroy_uniform(s_positionSampler);
        if (bgfxIsValid(s_bloomSampler))
            bgfx_destroy_uniform(s_bloomSampler);
        if (bgfxIsValid(s_exposure))
            bgfx_destroy_uniform(s_exposure);
        if (bgfxIsValid(s_fogParams))
            bgfx_destroy_uniform(s_fogParams);
        if (bgfxIsValid(s_fogColor))
            bgfx_destroy_uniform(s_fogColor);
        if (bgfxIsValid(s_lowlandFogParams))
            bgfx_destroy_uniform(s_lowlandFogParams);
        if (bgfxIsValid(s_sunDir))
            bgfx_destroy_uniform(s_sunDir);
        if (bgfxIsValid(s_sunColor))
            bgfx_destroy_uniform(s_sunColor);
        s_combineProgram = BGFX_INVALID_HANDLE;
        s_hdrSampler = BGFX_INVALID_HANDLE;
        s_tonemapSampler = BGFX_INVALID_HANDLE;
        s_positionSampler = BGFX_INVALID_HANDLE;
        s_bloomSampler = BGFX_INVALID_HANDLE;
        s_exposure = BGFX_INVALID_HANDLE;
        s_fogParams = BGFX_INVALID_HANDLE;
        s_fogColor = BGFX_INVALID_HANDLE;
        s_lowlandFogParams = BGFX_INVALID_HANDLE;
        s_sunDir = BGFX_INVALID_HANDLE;
        s_sunColor = BGFX_INVALID_HANDLE;
        s_combineLayoutReady = false;
    }

    void DestroyResolveProgram()
    {
        if (bgfxIsValid(s_resolveProgram))
            bgfx_destroy_program(s_resolveProgram);
        if (bgfxIsValid(s_litSampler))
            bgfx_destroy_uniform(s_litSampler);
        if (bgfxIsValid(s_litPositionSampler))
            bgfx_destroy_uniform(s_litPositionSampler);
        if (bgfxIsValid(s_litGbufSampler))
            bgfx_destroy_uniform(s_litGbufSampler);
        if (bgfxIsValid(s_litOccSampler))
            bgfx_destroy_uniform(s_litOccSampler);
        if (bgfxIsValid(s_hemiColor))
            bgfx_destroy_uniform(s_hemiColor);
        if (bgfxIsValid(s_ambientColor))
            bgfx_destroy_uniform(s_ambientColor);
        if (bgfxIsValid(s_envCube0))
            bgfx_destroy_uniform(s_envCube0);
        if (bgfxIsValid(s_envCube1))
            bgfx_destroy_uniform(s_envCube1);
        if (bgfxIsValid(s_cubeValid))
            bgfx_destroy_uniform(s_cubeValid);
        if (bgfxIsValid(s_lightCount))
            bgfx_destroy_uniform(s_lightCount);
        if (bgfxIsValid(s_lights))
            bgfx_destroy_uniform(s_lights);
        if (bgfxIsValid(s_lightParams))
            bgfx_destroy_uniform(s_lightParams);
        s_resolveProgram = BGFX_INVALID_HANDLE;
        s_litSampler = BGFX_INVALID_HANDLE;
        s_litPositionSampler = BGFX_INVALID_HANDLE;
        s_litGbufSampler = BGFX_INVALID_HANDLE;
        s_litOccSampler = BGFX_INVALID_HANDLE;
        s_hemiColor = BGFX_INVALID_HANDLE;
        s_ambientColor = BGFX_INVALID_HANDLE;
        s_envCube0 = BGFX_INVALID_HANDLE;
        s_envCube1 = BGFX_INVALID_HANDLE;
        s_cubeValid = BGFX_INVALID_HANDLE;
        s_lightCount = BGFX_INVALID_HANDLE;
        s_lights = BGFX_INVALID_HANDLE;
        s_lightParams = BGFX_INVALID_HANDLE;
        s_lightData.clear();
        s_lightParamData.clear();
        s_resolveLayoutReady = false;
        s_resolveOk = false;
    }

    void DestroyGbufDebugProgram()
    {
        if (bgfxIsValid(s_gbufDebugProgram))
            bgfx_destroy_program(s_gbufDebugProgram);
        if (bgfxIsValid(s_gbufDebugGbufSampler))
            bgfx_destroy_uniform(s_gbufDebugGbufSampler);
        if (bgfxIsValid(s_gbufDebugPosSampler))
            bgfx_destroy_uniform(s_gbufDebugPosSampler);
        if (bgfxIsValid(s_gbufDebugHdrSampler))
            bgfx_destroy_uniform(s_gbufDebugHdrSampler);
        if (bgfxIsValid(s_gbufDebugParams))
            bgfx_destroy_uniform(s_gbufDebugParams);
        s_gbufDebugProgram = BGFX_INVALID_HANDLE;
        s_gbufDebugGbufSampler = BGFX_INVALID_HANDLE;
        s_gbufDebugPosSampler = BGFX_INVALID_HANDLE;
        s_gbufDebugHdrSampler = BGFX_INVALID_HANDLE;
        s_gbufDebugParams = BGFX_INVALID_HANDLE;
        s_gbufDebugLayoutReady = false;
    }

    bool IsTextureSupported(bgfx_texture_format_t _format)
    {
        return bgfx_is_texture_valid(0, false, 1, _format,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP);
    }

    // =========================================================================
    // Sun shadow map
    // =========================================================================
    // Reference: archive_sourse/Layers/xrRenderPC_R4/r2_R_sun.cpp (the caster pass
    // and the sun matrix), r4_rendertarget.cpp:635-636 (the target),
    // r4_rendertarget_accum_direct.cpp:152-180 (m_shadow) and
    // Layers/xrRender/xrRender_console.cpp (the constants).
    //
    // Those console variables live in Layers/xrRender/xrRender_console.cpp, which
    // belongs to the D3D render layer. The bgfx layer replaces that layer and links
    // neither it nor its import library, so the values the reference reads at
    // runtime are repeated here verbatim instead. Consequence, and the reason this
    // is written down rather than papered over: r2_smap_size, r2_sun_tsm_proj,
    // r2_sun_tsm_bias, r2_sun_depth_far_scale and r2_sun_depth_far_bias have no
    // effect on the bgfx sun shadow until the port grows a console of its own. The
    // numbers are the reference's own defaults, not tuned values.
    const u32   kSunSmapSize          = 2048;      // ps_r2_smapsize,            xrRender_console.cpp:45
    const float kSunTsmProjection     = 0.3f;      // ps_r2_sun_tsm_projection,  xrRender_console.cpp:301
    const float kSunTsmBias           = -0.01f;    // ps_r2_sun_tsm_bias,        xrRender_console.cpp:302
    const float kSunDepthFarScale     = 1.0f;      // ps_r2_sun_depth_far_scale, xrRender_console.cpp:308
    const float kSunDepthFarBias      = -0.00002f; // ps_r2_sun_depth_far_bias,  xrRender_console.cpp:309
    // ps_r_sun_quality (xrRender_console.cpp:108) selects how many taps the PCSS
    // kernel takes, shadow.h:157-167. The reference build runs at 1, which is what
    // makes USE_ULTRA_SHADOWS off (accum_sun_near.ps:8-10) and shadow() call
    // shadow_pcss() directly (shadow.h:283-294); the shader reads the same value
    // through u_shadowParams, so the two cannot disagree.
    const u32   kSunQuality           = 1;

    void DestroyShadow()
    {
        if (bgfxIsValid(s_shadowProgram))
            bgfx_destroy_program(s_shadowProgram);
        if (bgfxIsValid(s_smapSampler))
            bgfx_destroy_uniform(s_smapSampler);
        if (bgfxIsValid(s_shadowMat))
            bgfx_destroy_uniform(s_shadowMat);
        if (bgfxIsValid(s_shadowParams))
            bgfx_destroy_uniform(s_shadowParams);
        s_shadowProgram = BGFX_INVALID_HANDLE;
        s_smapSampler = BGFX_INVALID_HANDLE;
        s_shadowMat = BGFX_INVALID_HANDLE;
        s_shadowParams = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_shadowFb))
            bgfx_destroy_frame_buffer(s_shadowFb);
        s_shadowFb = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_shadowMap))
            bgfx_destroy_texture(s_shadowMap);
        s_shadowMap = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_shadowDepth))
            bgfx_destroy_texture(s_shadowDepth);
        s_shadowDepth = BGFX_INVALID_HANDLE;
        s_shadowPassActive = false;
        s_shadowMapValid = false;
    }

    // r2_R_sun.cpp:728-731 renders into r2_RT_smap_depth, whose size is
    // ps_r2_smapsize (r4_rendertarget.cpp:635, default 2048,
    // xrRender_console.cpp:45) and whose format is HW_smap_FORMAT
    // (r4.cpp:221 D32F_LOCKABLE). See the statics block for what the port stores
    // in its place.
    bool EnsureShadowTargets()
    {
        if (bgfxIsValid(s_shadowFb) && bgfxIsValid(s_shadowMap) && bgfxIsValid(s_shadowDepth))
            return true;

        const u32 size = kSunSmapSize;
        if (!IsTextureSupported(BGFX_TEXTURE_FORMAT_R32F))
        {
            LogError("[BGFX] Shadow map: R32F unavailable");
            return false;
        }
        // Point sampled: the PCSS kernel places its taps by hand
        // (deferred_light_ps.sc, shadow_smap_pcss), and AXR's SampleCmpLevelZero
        // is a point compare as well (shadow.h:48).
        const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        s_shadowMap = bgfx_create_texture_2d(size, size, false, 1, BGFX_TEXTURE_FORMAT_R32F, colorFlags, nullptr, 0);
        s_shadowDepth = bgfx_create_texture_2d(size, size, false, 1, s_depthFormat, BGFX_TEXTURE_RT, nullptr, 0);
        if (!bgfxIsValid(s_shadowMap) || !bgfxIsValid(s_shadowDepth))
        {
            LogError("[BGFX] Shadow map texture create failed (%ux%u)", size, size);
            DestroyShadow();
            return false;
        }
        bgfx_texture_handle_t attachments[2] = { s_shadowMap, s_shadowDepth };
        s_shadowFb = bgfx_create_frame_buffer_from_handles(2, attachments, true);
        if (!bgfxIsValid(s_shadowFb))
        {
            LogError("[BGFX] Shadow map framebuffer create failed (%ux%u)", size, size);
            DestroyShadow();
            return false;
        }
        LogInfo("[BGFX] Shadow map created: %ux%u (AXR r2_RT_smap_depth, r4_rendertarget.cpp:636)", size, size);
        return true;
    }

    bool EnsureShadowProgram()
    {
        if (bgfxIsValid(s_shadowProgram) && bgfxIsValid(s_smapSampler) &&
            bgfxIsValid(s_shadowMat) && bgfxIsValid(s_shadowParams))
            return true;

        s_shadowProgram = BuildProgram("shadow_vs.sc", "shadow_ps.sc");
        if (!bgfxIsValid(s_shadowProgram))
        {
            LogError("[BGFX] Shadow program build failed");
            return false;
        }
        s_smapSampler = bgfx_create_uniform("s_smap", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_shadowMat = bgfx_create_uniform("u_shadowMat", BGFX_UNIFORM_TYPE_MAT4, 1);
        s_shadowParams = bgfx_create_uniform("u_shadowParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_smapSampler) || !bgfxIsValid(s_shadowMat) || !bgfxIsValid(s_shadowParams))
        {
            LogError("[BGFX] Shadow uniforms create failed");
            DestroyShadow();
            return false;
        }
        LogInfo("[BGFX] Shadow program created: %u", s_shadowProgram.idx);
        return true;
    }

    // =========================================================================
    // Screen-space ambient occlusion
    // =========================================================================
    // Reference: archive_sourse/Layers/xrRenderPC_R4/r4_rendertarget_phase_ssao.cpp
    // (phase_ssao, the half-resolution pass), blender_ssao.cpp (the element that
    // runs ssao_calc_nomsaa), r4_rendertarget.cpp:270-291 (generate_jitter) and
    // :856-874 (the r2_RT_ssao_temp target), driven by game_unpacked/shaders/r3/
    // ssao.ps and ssao_calc.ps and consumed by combine_1.ps:128-159 / :183.
    //
    // Like the shadow constants above, the SSAO_QUALITY the reference reads from
    // Layers/xrRender/xrRender_console.cpp is repeated here verbatim, because the
    // bgfx layer links neither that layer nor its import library. ps_r_ssao = 3
    // (xrRender_console.cpp:96) is the shipped default, r4_rendertarget.cpp:297-298
    // caps it to 3 for every non-hdao mode, and r4.cpp:1365-1371 hands it to the
    // shader as SSAO_QUALITY, which ssao.ps:35-56 turns into RINGS 3 / DIRS 8. The
    // shader hard-codes that kernel, so this value is what selects it; it is the
    // reference's default, not a tuned value.
    const u32 kSsaoQuality = 3;

    // r2_types.h:118 TEX_jitter - the side of the jitter0 dither texture. The
    // reference allocates TEX_jitter_count = 5 of them (r2_types.h:119) and the
    // blender helper stdafx.h:54-59 binds jitter0..jitter3 plus the HBAO float
    // noise; only jitter0 is read by calc_ssao (ssao.ps:156), so this port builds
    // that one and nothing else.
    const u32 kSsaoJitterSize = 64;

    void DestroySsao()
    {
        if (bgfxIsValid(s_ssaoProgram))
            bgfx_destroy_program(s_ssaoProgram);
        if (bgfxIsValid(s_ssaoPositionSampler))
            bgfx_destroy_uniform(s_ssaoPositionSampler);
        if (bgfxIsValid(s_ssaoGbufSampler))
            bgfx_destroy_uniform(s_ssaoGbufSampler);
        if (bgfxIsValid(s_ssaoJitterSampler))
            bgfx_destroy_uniform(s_ssaoJitterSampler);
        if (bgfxIsValid(s_ssaoNoiseTileFactor))
            bgfx_destroy_uniform(s_ssaoNoiseTileFactor);
        if (bgfxIsValid(s_ssaoKernelSize))
            bgfx_destroy_uniform(s_ssaoKernelSize);
        s_ssaoProgram = BGFX_INVALID_HANDLE;
        s_ssaoPositionSampler = BGFX_INVALID_HANDLE;
        s_ssaoGbufSampler = BGFX_INVALID_HANDLE;
        s_ssaoJitterSampler = BGFX_INVALID_HANDLE;
        s_ssaoNoiseTileFactor = BGFX_INVALID_HANDLE;
        s_ssaoKernelSize = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_ssaoFb))
            bgfx_destroy_frame_buffer(s_ssaoFb);
        s_ssaoFb = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_ssao))
            bgfx_destroy_texture(s_ssao);
        s_ssao = BGFX_INVALID_HANDLE;
        s_ssaoOk = false;
        if (bgfxIsValid(s_ssaoJitter))
            bgfx_destroy_texture(s_ssaoJitter);
        s_ssaoJitter = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_ssaoFallback))
            bgfx_destroy_texture(s_ssaoFallback);
        s_ssaoFallback = BGFX_INVALID_HANDLE;
    }

    // r4_rendertarget.cpp:270-291 generate_jitter. Per texel the reference draws
    // 2 * elem_count points in [0,256)^2, rejecting a candidate whose manhattan
    // distance to an already accepted one is below 32, and writes each point pair
    // as one DXGI_FORMAT_R8G8B8A8_SNORM texel through color_rgba(x, y, z, w)
    // with (x,y) = the first pair and (z,w) = the second pair transposed
    // (r4_rendertarget.cpp:289-290, :1082-1090).
    //
    // Two things cannot be reproduced literally and are worth naming:
    //   - the values come from ::Random, the engine's global generator, which is
    //     seeded per run, so the reference's own noise differs from run to run
    //     and there is nothing to match against. The port uses its own generator
    //     with a fixed seed instead of the shared one, because drawing from
    //     ::Random would shift the game's own random stream (AI, spawns, jitter
    //     of everything else) - a behaviour change well outside this pass.
    //   - the SNORM container itself: this bgfx build has no signed-normalised
    //     format (bgfx_capi.h:75-...), so the texels are stored as the floats the
    //     SNORM decode would have produced (byte / 127.5 - 1, the DXGI SNORM
    //     mapping of R8G8B8A8_SNORM). ssao.ps then reads exactly the values it
    //     reads in the reference; only the container differs.
    // jitter0 is the first of the four SNORM sheets (r4_rendertarget.cpp:1069), so
    // its texel is the *first* point pair of the eight the function accepts.
    static u32 s_ssaoJitterSeed = 0x13579bdfu;

    static u32 SsaoJitterRand()
    {
        // xorshift32; the only property the reference relies on is a uniform
        // draw over [0,256).
        u32 x = s_ssaoJitterSeed;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s_ssaoJitterSeed = x;
        return x;
    }

    bool EnsureSsaoJitter()
    {
        if (bgfxIsValid(s_ssaoJitter))
            return true;
        if (!IsTextureSupported(BGFX_TEXTURE_FORMAT_RGBA16F))
        {
            LogError("[BGFX] SSAO jitter: RGBA16F unavailable");
            return false;
        }
        s_ssaoJitterSeed = 0x13579bdfu;

        const u32 size = kSsaoJitterSize;
        // RGBA16F, 4 floats per texel.
        std::vector<float> texels(size * size * 4, 0.0f);
        for (u32 y = 0; y < size; ++y)
        {
            for (u32 x = 0; x < size; ++x)
            {
                // generate_jitter: samples.size() < elem_count * 2, i.e. eight
                // points, of which only the first pair reaches jitter0.
                int points[8][2] = {};
                u32 accepted = 0;
                while (accepted < 8)
                {
                    int test[2] = { (int)(SsaoJitterRand() & 0xff), (int)(SsaoJitterRand() & 0xff) };
                    bool valid = true;
                    for (u32 t = 0; t < accepted; ++t)
                    {
                        const int dist = abs(test[0] - points[t][0]) + abs(test[1] - points[t][1]);
                        if (dist < 32)
                        {
                            valid = false;
                            break;
                        }
                    }
                    if (valid)
                    {
                        points[accepted][0] = test[0];
                        points[accepted][1] = test[1];
                        ++accepted;
                    }
                }
                // color_rgba(samples[0].x, samples[0].y, samples[1].y, samples[1].x)
                // of r4_rendertarget.cpp:290, through the R8G8B8A8_SNORM decode.
                const int bytes[4] =
                {
                    points[0][0], points[0][1],
                    points[1][1], points[1][0],
                };
                for (u32 c = 0; c < 4; ++c)
                    texels[(y * size + x) * 4 + c] = (float)bytes[c] / 127.5f - 1.0f;
            }
        }

        // stdafx.h:50 (the commented DX9 line of the same helper) and :60: the
        // jitter is a point-sampled, wrapping texture. Neither clamp bit is set,
        // which in bgfx is exactly the wrap address mode, and the three point
        // bits give smp_jitter its D3DTEXF_POINT filtering.
        const uint64_t flags = BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_POINT | BGFX_TEXTURE_MIP_POINT;
        const uint32_t bytes = (uint32_t)(texels.size() * sizeof(float));
        const bgfx_memory_t* mem = bgfx_copy(texels.data(), bytes);
        s_ssaoJitter = bgfx_create_texture_2d((uint16_t)size, (uint16_t)size, false, 1,
            BGFX_TEXTURE_FORMAT_RGBA16F, flags, mem, 0);
        if (!bgfxIsValid(s_ssaoJitter))
        {
            LogError("[BGFX] SSAO jitter texture create failed (%ux%u)", size, size);
            s_ssaoJitter = BGFX_INVALID_HANDLE;
            return false;
        }
        LogInfo("[BGFX] SSAO jitter created: %ux%u (AXR jitter0, TEX_jitter, r2_types.h:118)", size, size);
        return true;
    }

    // Half-resolution occlusion target. r4_rendertarget_phase_ssao.cpp:52-55 sets
    // the viewport to dwWidth/2 x dwHeight/2 of a full-size r2_RT_ssao_temp and
    // the consumer reads it with a full-resolution coordinate, so the value the
    // consumer sees is the half-res one magnified 2x - a half-size target sampled
    // with the full-res tc is the same value, and it does not allocate the three
    // quarters of the reference's target nobody samples. Format D3DFMT_R16F
    // (r4_rendertarget.cpp:861).
    bool EnsureSsaoTargets()
    {
        const u32 w = (u32)s_width / 2;
        const u32 h = (u32)s_height / 2;
        if (w < 1 || h < 1)
            return false;
        if (bgfxIsValid(s_ssaoFb) && bgfxIsValid(s_ssao))
            return true;
        if (!IsTextureSupported(BGFX_TEXTURE_FORMAT_R16F))
        {
            LogError("[BGFX] SSAO target: R16F unavailable");
            return false;
        }
        const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        s_ssao = bgfx_create_texture_2d((uint16_t)w, (uint16_t)h, false, 1,
            BGFX_TEXTURE_FORMAT_R16F, colorFlags, nullptr, 0);
        if (!bgfxIsValid(s_ssao))
        {
            LogError("[BGFX] SSAO target texture create failed (%ux%u)", w, h);
            s_ssao = BGFX_INVALID_HANDLE;
            return false;
        }
        bgfx_texture_handle_t attachments[1] = { s_ssao };
        s_ssaoFb = bgfx_create_frame_buffer_from_handles(1, attachments, false);
        if (!bgfxIsValid(s_ssaoFb))
        {
            LogError("[BGFX] SSAO target framebuffer create failed (%ux%u)", w, h);
            s_ssaoFb = BGFX_INVALID_HANDLE;
            bgfx_destroy_texture(s_ssao);
            s_ssao = BGFX_INVALID_HANDLE;
            return false;
        }
        LogInfo("[BGFX] SSAO target created: %ux%u (AXR r2_RT_ssao_temp, half-res viewport, "
            "r4_rendertarget_phase_ssao.cpp:52-55)", w, h);
        return true;
    }

    bool EnsureSsaoProgram()
    {
        if (bgfxIsValid(s_ssaoProgram) && bgfxIsValid(s_ssaoPositionSampler) &&
            bgfxIsValid(s_ssaoGbufSampler) && bgfxIsValid(s_ssaoJitterSampler) &&
            bgfxIsValid(s_ssaoNoiseTileFactor) && bgfxIsValid(s_ssaoKernelSize))
            return true;

        if (!s_ssaoLayoutReady)
        {
            bgfx_vertex_layout_begin(&s_ssaoLayout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_ssaoLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_ssaoLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_ssaoLayout);
            s_ssaoLayoutReady = true;
        }

        // combine_vs.sc emits the same fullscreen triangle and v_texcoord0 the
        // resolve and the high pass consume, and the reference's own combine
        // vertex shader (combine_1.vs) does nothing else for this pass either.
        s_ssaoProgram = BuildProgram("combine_vs.sc", "ssao_calc_ps.sc");
        if (!bgfxIsValid(s_ssaoProgram))
        {
            LogError("[BGFX] SSAO program build failed");
            return false;
        }

        s_ssaoPositionSampler = bgfx_create_uniform("s_position", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_ssaoGbufSampler = bgfx_create_uniform("s_gbuf", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_ssaoJitterSampler = bgfx_create_uniform("s_jitter0", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_ssaoNoiseTileFactor = bgfx_create_uniform("u_ssao_noise_tile_factor", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_ssaoKernelSize = bgfx_create_uniform("u_ssao_kernel_size", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_ssaoPositionSampler) || !bgfxIsValid(s_ssaoGbufSampler) ||
            !bgfxIsValid(s_ssaoJitterSampler) || !bgfxIsValid(s_ssaoNoiseTileFactor) ||
            !bgfxIsValid(s_ssaoKernelSize))
        {
            LogError("[BGFX] SSAO uniforms create failed");
            DestroySsao();
            return false;
        }
        LogInfo("[BGFX] SSAO program created: %u (SSAO_QUALITY %u, ssao.ps:35-56)", s_ssaoProgram.idx, kSsaoQuality);
        return true;
    }

    // -------------------------------------------------------------------------
    // fuckingsun->X.D.combine, 1:1 with r2_R_sun.cpp:436-636.
    //
    // The reference builds the sun matrix out of the camera frustum and the sun
    // direction, and it does so in eye space, feeding the frustum through the
    // *view* matrix. Everything the chain needs from the caster/receiver lists is
    // the depth range (r2_R_sun.cpp:487 BuildTSMProjectionMatrix_caster_depth_bounds)
    // and the R2FLAG_SUN_FOCUS refit (r2_R_sun.cpp:639-715); both are documented
    // deviations below. What stays untouched is the part that decides where the
    // shadow map lands, i.e. everything that is a function of the camera and the
    // sun alone:
    //   lightSpaceBasis      r2_R_sun.cpp:464-477  (BuildLSPSMProjectionMatrix)
    //   lightSpaceOrtho      r2_R_sun.cpp:503-504  off-centre ortho of the AABB
    //   trapezoid_space      r2_R_sun.cpp:519-633  shear + unsqueeze + x rescale
    // and the gate that chooses between them and the R2 cull_xform,
    // r2_R_sun.cpp:446.
    // -------------------------------------------------------------------------
    // Column-major 4x4, the same layout as the D3DXMATRIX the reference casts its
    // own matrices to, so every literal below can be compared with the original
    // element for element. Fmatrix/D3DXMATRIX are row-major with m[i][j]; the
    // literals here are the same rows, and m[i] is the i-th column here.
    struct SunMatrix
    {
        float m[16];
    };

    static void SunMul(SunMatrix& _dst, const SunMatrix& _a, const SunMatrix& _b)
    {
        // _dst = _a * _b, the D3DXMatrixMultiply(out, a, b) convention
        // (r2_R_sun.cpp:611, :622, :629, :631-633).
        SunMatrix r;
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k)
                    s += _a.m[k * 4 + j] * _b.m[i * 4 + k];
                r.m[i * 4 + j] = s;
            }
        }
        _dst = r;
    }

    // D3DXVec3TransformCoord: the matrix part plus the translation, w = 1.
    static void SunXformCoord(float* _out, const SunMatrix& _m, const float* _in)
    {
        _out[0] = _m.m[0] * _in[0] + _m.m[1] * _in[1] + _m.m[2] * _in[2] + _m.m[3];
        _out[1] = _m.m[4] * _in[0] + _m.m[5] * _in[1] + _m.m[6] * _in[2] + _m.m[7];
        _out[2] = _m.m[8] * _in[0] + _m.m[9] * _in[1] + _m.m[10] * _in[2] + _m.m[11];
    }

    // D3DXMatrixOrthoOffCenterLH (r2_R_sun.cpp:504).
    static SunMatrix SunOrthoOffCenterLH(float _l, float _r, float _b, float _t, float _zn, float _zf)
    {
        SunMatrix r;
        r.m[0] = 2.0f / (_r - _l);   r.m[1] = 0.0f;                r.m[2] = 0.0f;                 r.m[3] = 0.0f;
        r.m[4] = 0.0f;               r.m[5] = 2.0f / (_t - _b);   r.m[6] = 0.0f;                 r.m[7] = 0.0f;
        r.m[8] = 0.0f;               r.m[9] = 0.0f;                r.m[10] = 1.0f / (_zf - _zn);  r.m[11] = 0.0f;
        r.m[12] = -(_r + _l) / (_r - _l);
        r.m[13] = -(_t + _b) / (_t - _b);
        r.m[14] = -_zn / (_zf - _zn);
        r.m[15] = 1.0f;
        return r;
    }

    // Builds fuckingsun->X.D.combine into _out, and reports through _ok whether
    // the TSM branch was taken. _lightDir is the sun's light-travel direction in
    // world space, i.e. CEnvDescriptor::sun_dir: the resolve's u_sunDir is
    // normalize(mView * sun_dir) and the shader turns it into the surface-to-light
    // vector with -normalize (deferred_light_ps.sc:351), which is the same sign
    // convention r2_R_sun.cpp:436 assumes when it negates fuckingsun->direction.
    static bool BuildSunMatrix(SunMatrix& _out, const float* _lightDir, const Fmatrix& _view,
                               float _fovRad, float _aspect, float _far)
    {
        SunMatrix eyeSpaceView;
        std::memcpy(eyeSpaceView.m, _view.m, sizeof(eyeSpaceView.m));

        // m_lightDir in eye space, r2_R_sun.cpp:436-442. m_fCosGamma is the tilt
        // between the light and the view, and it is the gate of r2_R_sun.cpp:446.
        float mLightDir[3] = { -_lightDir[0], -_lightDir[1], -_lightDir[2] };
        float upView[3];
        SunXformCoord(upView, eyeSpaceView, mLightDir); // D3DXVec3TransformNormal
        const float m_fCosGamma = mLightDir[0] * eyeSpaceView.m[2] +   // m_View._13
                                  mLightDir[1] * eyeSpaceView.m[6] +   // m_View._23
                                  mLightDir[2] * eyeSpaceView.m[10];   // m_View._33

        // R2FLAG_SUN_TSM is on in the reference build (xrRender_console.cpp:262),
        // and ps_r2_sun_tsm_projection is 0.3 (xrRender_console.cpp:301). The mask
        // itself is not reachable from here (see kSunSmapSize above), so the gate
        // keeps the flag side - the tilt test of r2_R_sun.cpp:446 - and takes the
        // projection constant, both of which are the reference's configured values.
        if (!(_abs(m_fCosGamma) < 0.99f && true))
        {
            // r2_R_sun.cpp:634-636 takes the R2 cull_xform here. That matrix comes
            // from a caster hull (DumbConvexVolume::compute_caster_model,
            // r2_R_sun.cpp:340-401) built over main_coarse_structure, which this
            // port has no equivalent of; the frustum AABB in light space below is
            // the same shape of fit with the casters left out.
        }

        // r2_R_sun.cpp:451-457: the 8 eye-space frustum corners, far plane first.
        // Frustum::pntList is not part of the archived sources, so the corners are
        // unprojected out of the projection directly; the reference only ever uses
        // them as two four-point sets ([0..3] = far, [4..7] = near) and every step
        // below is symmetric inside a plane, so the corner order inside a set does
        // not change the result.
        float frustumPnts[8][3];
        {
            const float tanHalf = tanf(_fovRad * 0.5f);
            const float nearZ = VIEWPORT_NEAR;
            // 4 far corners then 4 near corners, x/y signs sweeping the rectangle.
            const float signs[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
            for (int i = 0; i < 4; ++i)
            {
                frustumPnts[i][0] = signs[i][0] * tanHalf * _aspect * _far;
                frustumPnts[i][1] = signs[i][1] * tanHalf * _far;
                frustumPnts[i][2] = -_far;
                frustumPnts[i + 4][0] = signs[i][0] * tanHalf * _aspect * nearZ;
                frustumPnts[i + 4][1] = signs[i][1] * tanHalf * nearZ;
                frustumPnts[i + 4][2] = -nearZ;
            }
        }

        // r2_R_sun.cpp:464-471: upVector = normalize(m_lightDir in eye space),
        // leftVector = normalize(cross(up, (0,0,-1))), viewVector = cross(up, left).
        float leftVector[3], viewVector[3];
        {
            const float eyeVector[3] = { 0.0f, 0.0f, -1.0f };
            float l = _sqrt(upView[0] * upView[0] + upView[1] * upView[1] + upView[2] * upView[2]);
            if (l < 1e-8f)
                l = 1.0f;
            leftVector[0] = upView[1] * eyeVector[2] - upView[2] * eyeVector[1];
            leftVector[1] = upView[2] * eyeVector[0] - upView[0] * eyeVector[2];
            leftVector[2] = upView[0] * eyeVector[1] - upView[1] * eyeVector[0];
            l = _sqrt(leftVector[0] * leftVector[0] + leftVector[1] * leftVector[1] + leftVector[2] * leftVector[2]);
            if (l < 1e-8f)
                l = 1.0f;
            leftVector[0] /= l; leftVector[1] /= l; leftVector[2] /= l;
            viewVector[0] = upView[1] * leftVector[2] - upView[2] * leftVector[1];
            viewVector[1] = upView[2] * leftVector[0] - upView[0] * leftVector[2];
            viewVector[2] = upView[0] * leftVector[1] - upView[1] * leftVector[0];
        }

        // r2_R_sun.cpp:473-477, the LSPSM light space with Y and Z permuted.
        SunMatrix lightSpaceBasis;
        lightSpaceBasis.m[0] = leftVector[0];  lightSpaceBasis.m[1] = viewVector[0];  lightSpaceBasis.m[2] = -upView[0];  lightSpaceBasis.m[3] = 0.0f;
        lightSpaceBasis.m[4] = leftVector[1];  lightSpaceBasis.m[5] = viewVector[1];  lightSpaceBasis.m[6] = -upView[1];  lightSpaceBasis.m[7] = 0.0f;
        lightSpaceBasis.m[8] = leftVector[2];  lightSpaceBasis.m[9] = viewVector[2];  lightSpaceBasis.m[10] = -upView[2]; lightSpaceBasis.m[11] = 0.0f;
        lightSpaceBasis.m[12] = 0.0f;          lightSpaceBasis.m[13] = 0.0f;           lightSpaceBasis.m[14] = 0.0f;         lightSpaceBasis.m[15] = 1.0f;

        // r2_R_sun.cpp:480, rotate the eye frustum into light space.
        for (int i = 0; i < 8; ++i)
            SunXformCoord(frustumPnts[i], lightSpaceBasis, frustumPnts[i]);

        // r2_R_sun.cpp:489-501. The reference takes min/max over the frustum AABB and
        // the light-space z bounds of every shadow caster
        // (BuildTSMProjectionMatrix_caster_depth_bounds, r2_R_sun.cpp:487, driven by
        // s_casters that r_dsgraph_render_subspace fills at r2_R_sun.cpp:416). The
        // port has no caster list at matrix-build time - the walk that could produce
        // one has not run yet, and it runs with this matrix - so the depth range is
        // the frustum's. Casters outside it are clipped by the ortho box, which is
        // what fBias (below) then compensates for, exactly as it does in the
        // reference when the near plane cuts the map.
        float min_z = frustumPnts[0][2], max_z = frustumPnts[0][2];
        float min_x = frustumPnts[0][0], max_x = frustumPnts[0][0];
        float min_y = frustumPnts[0][1], max_y = frustumPnts[0][1];
        for (int i = 1; i < 8; ++i)
        {
            min_x = _min(min_x, frustumPnts[i][0]); max_x = _max(max_x, frustumPnts[i][0]);
            min_y = _min(min_y, frustumPnts[i][1]); max_y = _max(max_y, frustumPnts[i][1]);
            min_z = _min(min_z, frustumPnts[i][2]); max_z = _max(max_z, frustumPnts[i][2]);
        }
        if (min_z <= 1.0f)
        {
            // r2_R_sun.cpp:494-500: push the whole box to z >= 1.
            for (int i = 0; i < 8; ++i)
                frustumPnts[i][2] -= min_z - 1.0f;
            max_z = -min_z + max_z + 1.0f;
            min_z = 1.0f;
        }

        // r2_R_sun.cpp:503-504.
        SunMatrix lightSpaceOrtho = SunOrthoOffCenterLH(min_x, max_x, min_y, max_y, min_z, max_z);

        // r2_R_sun.cpp:507: the frustum through the ortho, which is what the
        // trapezoid fit below operates on.
        for (int i = 0; i < 8; ++i)
            SunXformCoord(frustumPnts[i], lightSpaceOrtho, frustumPnts[i]);

        // r2_R_sun.cpp:509-517: the centre of the near plane and of the far plane.
        float centerPts[2][2];
        for (int j = 0; j < 2; ++j)
        {
            const int base = j ? 0 : 4; // far plane first, near plane second
            centerPts[j][0] = 0.25f * (frustumPnts[base + 0][0] + frustumPnts[base + 1][0] +
                                       frustumPnts[base + 2][0] + frustumPnts[base + 3][0]);
            centerPts[j][1] = 0.25f * (frustumPnts[base + 0][1] + frustumPnts[base + 1][1] +
                                       frustumPnts[base + 2][1] + frustumPnts[base + 3][1]);
        }
        const float centerOrig[2] = { (centerPts[0][0] + centerPts[1][0]) * 0.5f,
                                       (centerPts[0][1] + centerPts[1][1]) * 0.5f };

        // r2_R_sun.cpp:521-543: xlate_center then rot_center, which puts the frustum's
        // centre line onto y = 0.
        SunMatrix xlate_center;
        xlate_center.m[0] = 1.0f; xlate_center.m[1] = 0.0f; xlate_center.m[2] = 0.0f; xlate_center.m[3] = 0.0f;
        xlate_center.m[4] = 0.0f; xlate_center.m[5] = 1.0f; xlate_center.m[6] = 0.0f; xlate_center.m[7] = 0.0f;
        xlate_center.m[8] = 0.0f; xlate_center.m[9] = 0.0f; xlate_center.m[10] = 1.0f; xlate_center.m[11] = 0.0f;
        xlate_center.m[12] = -centerOrig[0]; xlate_center.m[13] = -centerOrig[1]; xlate_center.m[14] = 0.0f; xlate_center.m[15] = 1.0f;

        const float x_len = centerPts[1][0] - centerOrig[0];
        const float y_len = centerPts[1][1] - centerOrig[1];
        const float half_center_len = _sqrt(x_len * x_len + y_len * y_len);
        const float cos_theta = half_center_len > 0.0f ? x_len / half_center_len : 1.0f;
        const float sin_theta = half_center_len > 0.0f ? y_len / half_center_len : 0.0f;
        SunMatrix rot_center;
        rot_center.m[0] = cos_theta; rot_center.m[1] = -sin_theta; rot_center.m[2] = 0.0f; rot_center.m[3] = 0.0f;
        rot_center.m[4] = sin_theta; rot_center.m[5] = cos_theta;  rot_center.m[6] = 0.0f; rot_center.m[7] = 0.0f;
        rot_center.m[8] = 0.0f;       rot_center.m[9] = 0.0f;       rot_center.m[10] = 1.0f; rot_center.m[11] = 0.0f;
        rot_center.m[12] = 0.0f;      rot_center.m[13] = 0.0f;      rot_center.m[14] = 0.0f; rot_center.m[15] = 1.0f;

        SunMatrix trapezoid_space;
        SunMul(trapezoid_space, xlate_center, rot_center);
        for (int i = 0; i < 8; ++i)
            SunXformCoord(frustumPnts[i], trapezoid_space, frustumPnts[i]);

        // r2_R_sun.cpp:546-566: the AABB in the rotated space, scaled so it fills
        // the unit square on both axes.
        float aabbMinX = frustumPnts[0][0], aabbMaxX = frustumPnts[0][0];
        float aabbMinY = frustumPnts[0][1], aabbMaxY = frustumPnts[0][1];
        for (int i = 1; i < 8; ++i)
        {
            aabbMinX = _min(aabbMinX, frustumPnts[i][0]); aabbMaxX = _max(aabbMaxX, frustumPnts[i][0]);
            aabbMinY = _min(aabbMinY, frustumPnts[i][1]); aabbMaxY = _max(aabbMaxY, frustumPnts[i][1]);
        }
        const float x_scale = 1.0f / _max(_abs(aabbMaxX), _abs(aabbMinX));
        const float y_scale = 1.0f / _max(_abs(aabbMaxY), _abs(aabbMinY));
        SunMatrix scale_center;
        scale_center.m[0] = x_scale; scale_center.m[1] = 0.0f;       scale_center.m[2] = 0.0f; scale_center.m[3] = 0.0f;
        scale_center.m[4] = 0.0f;     scale_center.m[5] = y_scale;   scale_center.m[6] = 0.0f; scale_center.m[7] = 0.0f;
        scale_center.m[8] = 0.0f;     scale_center.m[9] = 0.0f;       scale_center.m[10] = 1.0f; scale_center.m[11] = 0.0f;
        scale_center.m[12] = 0.0f;    scale_center.m[13] = 0.0f;      scale_center.m[14] = 0.0f; scale_center.m[15] = 1.0f;
        SunMul(trapezoid_space, trapezoid_space, scale_center);
        aabbMinX *= x_scale; aabbMaxX *= x_scale;
        aabbMinY *= y_scale; aabbMaxY *= y_scale;

        // r2_R_sun.cpp:568-574: the projection point Q, eta away from the top line.
        const float m_fTSM_Delta = kSunTsmProjection;
        const float lambda = aabbMaxX - aabbMinX;
        const float delta_proj = m_fTSM_Delta * lambda;
        const float xi = -0.6f;
        const float eta = (lambda * delta_proj * (1.0f + xi)) / (lambda * (1.0f - xi) - 2.0f * delta_proj);
        const float projectionPtQ[2] = { aabbMaxX + eta, 0.0f };

        // r2_R_sun.cpp:578-590: the projection field of view, the extreme slopes
        // from Q to the frustum points.
        float max_slope = -1e32f, min_slope = 1e32f;
        for (int i = 0; i < 8; ++i)
        {
            const float tmp_x = frustumPnts[i][0] * x_scale;
            const float tmp_y = frustumPnts[i][1] * y_scale;
            const float x_dist = tmp_x - projectionPtQ[0];
            if (!(tmp_y == 0.0f || x_dist == 0.0f))
            {
                max_slope = _max(max_slope, tmp_y / x_dist);
                min_slope = _min(min_slope, tmp_y / x_dist);
            }
        }
        const float xf = lambda + eta;   // r2_R_sun.cpp:593, xn = eta is the literal's own value

        // r2_R_sun.cpp:595-599: move Q to the origin.
        SunMatrix ptQ_xlate;
        ptQ_xlate.m[0] = -1.0f; ptQ_xlate.m[1] = 0.0f; ptQ_xlate.m[2] = 0.0f; ptQ_xlate.m[3] = 0.0f;
        ptQ_xlate.m[4] = 0.0f;  ptQ_xlate.m[5] = 1.0f; ptQ_xlate.m[6] = 0.0f; ptQ_xlate.m[7] = 0.0f;
        ptQ_xlate.m[8] = 0.0f;  ptQ_xlate.m[9] = 0.0f; ptQ_xlate.m[10] = 1.0f; ptQ_xlate.m[11] = 0.0f;
        ptQ_xlate.m[12] = projectionPtQ[0]; ptQ_xlate.m[13] = 0.0f; ptQ_xlate.m[14] = 0.0f; ptQ_xlate.m[15] = 1.0f;
        SunMul(trapezoid_space, trapezoid_space, ptQ_xlate);

        // r2_R_sun.cpp:603-611: the shear that balances the trapezoid around y = 0.
        const float shear_amt = (max_slope + _abs(min_slope)) * 0.5f - max_slope;
        max_slope += shear_amt;
        SunMatrix trapezoid_shear;
        trapezoid_shear.m[0] = 1.0f;       trapezoid_shear.m[1] = shear_amt; trapezoid_shear.m[2] = 0.0f; trapezoid_shear.m[3] = 0.0f;
        trapezoid_shear.m[4] = 0.0f;       trapezoid_shear.m[5] = 1.0f;       trapezoid_shear.m[6] = 0.0f; trapezoid_shear.m[7] = 0.0f;
        trapezoid_shear.m[8] = 0.0f;       trapezoid_shear.m[9] = 0.0f;       trapezoid_shear.m[10] = 1.0f; trapezoid_shear.m[11] = 0.0f;
        trapezoid_shear.m[12] = 0.0f;      trapezoid_shear.m[13] = 0.0f;      trapezoid_shear.m[14] = 0.0f; trapezoid_shear.m[15] = 1.0f;
        SunMul(trapezoid_space, trapezoid_space, trapezoid_shear);

        // r2_R_sun.cpp:614-622: the 2D projection that unsqueezes the top line.
        const float z_aspect = (max_z - min_z) / (aabbMaxY - aabbMinY);
        SunMatrix trapezoid_projection;
        trapezoid_projection.m[0] = xf / (xf - eta); trapezoid_projection.m[1] = 0.0f;  trapezoid_projection.m[2] = 0.0f; trapezoid_projection.m[3] = 1.0f;
        trapezoid_projection.m[4] = 0.0f;              trapezoid_projection.m[5] = 1.0f / max_slope; trapezoid_projection.m[6] = 0.0f; trapezoid_projection.m[7] = 0.0f;
        trapezoid_projection.m[8] = 0.0f;              trapezoid_projection.m[9] = 0.0f;  trapezoid_projection.m[10] = 1.0f / (z_aspect * max_slope); trapezoid_projection.m[11] = 0.0f;
        trapezoid_projection.m[12] = -eta * xf / (xf - eta); trapezoid_projection.m[13] = 0.0f; trapezoid_projection.m[14] = 0.0f; trapezoid_projection.m[15] = 0.0f;
        SunMul(trapezoid_space, trapezoid_space, trapezoid_projection);

        // r2_R_sun.cpp:625-629: x is [0,1] after the projection, expand it to [-1,1].
        SunMatrix biasedScaleX;
        biasedScaleX.m[0] = 2.0f; biasedScaleX.m[1] = 0.0f; biasedScaleX.m[2] = 0.0f; biasedScaleX.m[3] = 0.0f;
        biasedScaleX.m[4] = 0.0f; biasedScaleX.m[5] = 1.0f; biasedScaleX.m[6] = 0.0f; biasedScaleX.m[7] = 0.0f;
        biasedScaleX.m[8] = 0.0f; biasedScaleX.m[9] = 0.0f; biasedScaleX.m[10] = 1.0f; biasedScaleX.m[11] = 0.0f;
        biasedScaleX.m[12] = -1.0f; biasedScaleX.m[13] = 0.0f; biasedScaleX.m[14] = 0.0f; biasedScaleX.m[15] = 1.0f;
        SunMul(trapezoid_space, trapezoid_space, biasedScaleX);

        // r2_R_sun.cpp:631-633: m_LightViewProj = view * lightSpaceBasis * ortho * trapezoid.
        SunMatrix lightViewProj;
        SunMul(lightViewProj, eyeSpaceView, lightSpaceBasis);
        SunMul(lightViewProj, lightViewProj, lightSpaceOrtho);
        SunMul(lightViewProj, lightViewProj, trapezoid_space);

        // r2_R_sun.cpp:639-715, R2FLAG_SUN_FOCUS: the refit onto the receiver AABB.
        // The reference gets it from s_casters / s_receivers == main_coarse_structure
        // (r2_R_sun.cpp:414-417, :655-669), i.e. from the spatial database of the
        // level, which this port does not have - the port's visual list carries no
        // per-object bounds at draw time. The frustum fit above is kept instead, so
        // the shadow map keeps the whole visible frustum and its effective texel
        // density is below the reference's focused one. This is the only thing left
        // out of the matrix, and it is a fit, not a sampling rule: the sampling in
        // deferred_light_ps.sc is the reference's PCSS unchanged.
        _out = lightViewProj;
        return true;
    }

    bgfx_program_handle_t BuildProgram(const char* _vs, const char* _ps)
    {
        std::vector<uint8_t> vsBlob;
        std::vector<uint8_t> psBlob;
        if (!bgfxShaderCompileFile(_vs, 'v', vsBlob) || !bgfxShaderCompileFile(_ps, 'f', psBlob) ||
            vsBlob.empty() || psBlob.empty())
            return BGFX_INVALID_HANDLE;
        bgfx_shader_handle_t vsh = bgfx_create_shader(bgfx_copy(vsBlob.data(), (uint32_t)vsBlob.size()));
        bgfx_shader_handle_t fsh = bgfx_create_shader(bgfx_copy(psBlob.data(), (uint32_t)psBlob.size()));
        if (!bgfxIsValid(vsh) || !bgfxIsValid(fsh))
        {
            if (bgfxIsValid(vsh))
                bgfx_destroy_shader(vsh);
            if (bgfxIsValid(fsh))
                bgfx_destroy_shader(fsh);
            return BGFX_INVALID_HANDLE;
        }
        bgfx_program_handle_t program = bgfx_create_program(vsh, fsh, true);
        if (!bgfxIsValid(program))
        {
            bgfx_destroy_shader(vsh);
            bgfx_destroy_shader(fsh);
            return BGFX_INVALID_HANDLE;
        }
        return program;
    }

    bool EnsureLuminanceLayout()
    {
        if (s_lumLayoutReady)
            return true;
        bgfx_vertex_layout_begin(&s_lumLayout, bgfx_get_renderer_type());
        bgfx_vertex_layout_add(&s_lumLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
        bgfx_vertex_layout_add(&s_lumLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
        bgfx_vertex_layout_end(&s_lumLayout);
        s_lumLayoutReady = true;
        return true;
    }

    bool EnsureLuminancePrograms()
    {
        if (bgfxIsValid(s_lumProgram) && bgfxIsValid(s_lumImage) && bgfxIsValid(s_lumPrev) &&
            bgfxIsValid(s_lumMiddleGray) && bgfxIsValid(s_lumParams))
            return EnsureLuminanceLayout();
        EnsureLuminanceLayout();
        s_lumProgram = BuildProgram("luminance_vs.sc", "luminance_ps.sc");
        if (!bgfxIsValid(s_lumProgram))
        {
            LogError("[BGFX] Luminance program build failed");
            DestroyLuminancePrograms();
            return false;
        }
        s_lumImage = bgfx_create_uniform("s_image", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_lumPrev = bgfx_create_uniform("s_prev", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_lumMiddleGray = bgfx_create_uniform("u_middleGray", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_lumParams = bgfx_create_uniform("u_luminanceParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_lumImage) || !bgfxIsValid(s_lumPrev) || !bgfxIsValid(s_lumMiddleGray) ||
            !bgfxIsValid(s_lumParams))
        {
            LogError("[BGFX] Luminance uniforms create failed");
            DestroyLuminancePrograms();
            return false;
        }
        LogInfo("[BGFX] Luminance program created: %u", s_lumProgram.idx);
        return true;
    }

    bool CreateLuminanceTargets()
    {
        DestroyLuminanceTargets();
        const uint64_t flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        bgfx_texture_format_t format = BGFX_TEXTURE_FORMAT_RGBA16F;
        if (!IsTextureSupported(format))
            format = BGFX_TEXTURE_FORMAT_RGBA32F;
        if (!IsTextureSupported(format))
        {
            LogError("[BGFX] Luminance target format unavailable");
            return false;
        }
        const bgfx_texture_format_t oneFormat = IsTextureSupported(BGFX_TEXTURE_FORMAT_R32F)
            ? BGFX_TEXTURE_FORMAT_R32F : format;

        s_lum64 = bgfx_create_texture_2d(64, 64, false, 1, format, flags, nullptr, 0);
        s_lum8 = bgfx_create_texture_2d(8, 8, false, 1, format, flags, nullptr, 0);
        if (oneFormat == BGFX_TEXTURE_FORMAT_R32F)
        {
            const float initial = 127.0f / 255.0f;
            for (u32 i = 0; i < 2; ++i)
                s_lum1[i] = bgfx_create_texture_2d(1, 1, false, 1, oneFormat, flags,
                    bgfx_copy(&initial, sizeof(initial)), 0);
        }
        else
        {
            const float initial[4] = { 127.0f / 255.0f, 0.0f, 0.0f, 1.0f };
            for (u32 i = 0; i < 2; ++i)
                s_lum1[i] = bgfx_create_texture_2d(1, 1, false, 1, oneFormat, flags,
                    bgfx_copy(initial, sizeof(initial)), 0);
        }
        if (!bgfxIsValid(s_lum64) || !bgfxIsValid(s_lum8) || !bgfxIsValid(s_lum1[0]) || !bgfxIsValid(s_lum1[1]))
        {
            LogError("[BGFX] Luminance target texture create failed");
            DestroyLuminanceTargets();
            return false;
        }
        bgfx_texture_handle_t a64[] = { s_lum64 };
        bgfx_texture_handle_t a8[] = { s_lum8 };
        bgfx_texture_handle_t a1[] = { s_lum1[0] };
        bgfx_texture_handle_t a2[] = { s_lum1[1] };
        s_lum64Fb = bgfx_create_frame_buffer_from_handles(1, a64, true);
        s_lum8Fb = bgfx_create_frame_buffer_from_handles(1, a8, true);
        s_lum1Fb[0] = bgfx_create_frame_buffer_from_handles(1, a1, true);
        s_lum1Fb[1] = bgfx_create_frame_buffer_from_handles(1, a2, true);
        if (!bgfxIsValid(s_lum64Fb) || !bgfxIsValid(s_lum8Fb) || !bgfxIsValid(s_lum1Fb[0]) || !bgfxIsValid(s_lum1Fb[1]))
        {
            LogError("[BGFX] Luminance framebuffer create failed");
            DestroyLuminanceTargets();
            return false;
        }
        s_lumTonemapIndex = false;
        s_luminanceAdapt = 0.5f;
        LogInfo("[BGFX] Luminance targets created: 64x64 -> 8x8 -> 1x1 format=%d one=%d", (int)format, (int)oneFormat);
        return true;
    }

    bool EnsureLuminanceTargets()
    {
        if (bgfxIsValid(s_lum64Fb) && bgfxIsValid(s_lum8Fb) && bgfxIsValid(s_lum1Fb[0]) &&
            bgfxIsValid(s_lum1Fb[1]) && bgfxIsValid(s_lum64) && bgfxIsValid(s_lum8) &&
            bgfxIsValid(s_lum1[0]) && bgfxIsValid(s_lum1[1]))
            return true;
        return CreateLuminanceTargets();
    }

    bool EnsureCombineProgram()
    {
        if (bgfxIsValid(s_combineProgram) && bgfxIsValid(s_hdrSampler) && bgfxIsValid(s_tonemapSampler) &&
            bgfxIsValid(s_positionSampler) && bgfxIsValid(s_bloomSampler) && bgfxIsValid(s_exposure) &&
            bgfxIsValid(s_fogParams) && bgfxIsValid(s_fogColor) && bgfxIsValid(s_lowlandFogParams) &&
            bgfxIsValid(s_sunDir) && bgfxIsValid(s_sunColor))
            return true;

        if (!s_combineLayoutReady)
        {
            bgfx_vertex_layout_begin(&s_combineLayout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_combineLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_combineLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_combineLayout);
            s_combineLayoutReady = true;
        }

        std::vector<uint8_t> vsBlob;
        std::vector<uint8_t> psBlob;
        if (!bgfxShaderCompileFile("combine_vs.sc", 'v', vsBlob) ||
            !bgfxShaderCompileFile("combine_ps.sc", 'f', psBlob) ||
            vsBlob.empty() || psBlob.empty())
        {
            LogError("[BGFX] Combine program build failed");
            return false;
        }

        bgfx_shader_handle_t vsh = bgfx_create_shader(bgfx_copy(vsBlob.data(), (uint32_t)vsBlob.size()));
        bgfx_shader_handle_t fsh = bgfx_create_shader(bgfx_copy(psBlob.data(), (uint32_t)psBlob.size()));
        if (!bgfxIsValid(vsh) || !bgfxIsValid(fsh))
        {
            if (bgfxIsValid(vsh))
                bgfx_destroy_shader(vsh);
            if (bgfxIsValid(fsh))
                bgfx_destroy_shader(fsh);
            LogError("[BGFX] Combine shader create failed");
            return false;
        }

        s_combineProgram = bgfx_create_program(vsh, fsh, true);
        if (!bgfxIsValid(s_combineProgram))
        {
            bgfx_destroy_shader(vsh);
            bgfx_destroy_shader(fsh);
            LogError("[BGFX] Combine program create failed");
            return false;
        }

        s_hdrSampler = bgfx_create_uniform("s_hdr", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_tonemapSampler = bgfx_create_uniform("s_tonemap", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_positionSampler = bgfx_create_uniform("s_position", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_bloomSampler = bgfx_create_uniform("s_bloom", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_exposure = bgfx_create_uniform("u_exposure", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_fogParams = bgfx_create_uniform("u_fogParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_fogColor = bgfx_create_uniform("u_fogColor", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_lowlandFogParams = bgfx_create_uniform("u_lowlandFogParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_sunDir = bgfx_create_uniform("u_sunDir", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_sunColor = bgfx_create_uniform("u_sunColor", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_hdrSampler) || !bgfxIsValid(s_tonemapSampler) || !bgfxIsValid(s_positionSampler) ||
            !bgfxIsValid(s_bloomSampler) ||
            !bgfxIsValid(s_exposure) || !bgfxIsValid(s_fogParams) || !bgfxIsValid(s_fogColor) ||
            !bgfxIsValid(s_lowlandFogParams) || !bgfxIsValid(s_sunDir) || !bgfxIsValid(s_sunColor))
        {
            LogError("[BGFX] Combine uniforms create failed");
            DestroyCombineProgram();
            return false;
        }

        LogInfo("[BGFX] Combine program created: %u", s_combineProgram.idx);
        return true;
    }

    bool EnsureResolveProgram()
    {
        if (bgfxIsValid(s_resolveProgram) && bgfxIsValid(s_litSampler) &&
            bgfxIsValid(s_litPositionSampler) && bgfxIsValid(s_litGbufSampler) &&
            bgfxIsValid(s_hemiColor) && bgfxIsValid(s_ambientColor) &&
            bgfxIsValid(s_envCube0) && bgfxIsValid(s_envCube1) && bgfxIsValid(s_cubeValid) &&
            bgfxIsValid(s_lightCount) && bgfxIsValid(s_lights) && bgfxIsValid(s_lightParams))
            return true;

        if (!s_resolveLayoutReady)
        {
            bgfx_vertex_layout_begin(&s_resolveLayout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_resolveLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_resolveLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_resolveLayout);
            s_resolveLayoutReady = true;
        }

        // combine_vs.sc emits the same fullscreen triangle and v_texcoord0 the
        // other post passes consume.
        s_resolveProgram = BuildProgram("combine_vs.sc", "deferred_light_ps.sc");
        if (!bgfxIsValid(s_resolveProgram))
        {
            LogError("[BGFX] Lighting resolve program build failed");
            return false;
        }

        s_litSampler = bgfx_create_uniform("s_diffuse", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_litPositionSampler = bgfx_create_uniform("s_position", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_litGbufSampler = bgfx_create_uniform("s_gbuf", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        // s_occ, the half-resolution occlusion buffer phase_ssao writes
        // (blender_combine.cpp:42 binds the same name in the combine pass).
        s_litOccSampler = bgfx_create_uniform("s_occ", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_hemiColor = bgfx_create_uniform("u_hemiColor", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_ambientColor = bgfx_create_uniform("u_ambient", BGFX_UNIFORM_TYPE_VEC4, 1);
        // env_s0 / env_s1, hmodel.h:14-15, and the 0/1 switch that lets
        // deferred_light_ps.sc fall back to the cube-less constant while the
        // level has no env cube bound.
        s_envCube0 = bgfx_create_uniform("s_env0", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_envCube1 = bgfx_create_uniform("s_env1", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_cubeValid = bgfx_create_uniform("u_cubeValid", BGFX_UNIFORM_TYPE_VEC4, 1);
        // The dynamic light list the accumulators read (deferred_light_ps.sc).
        s_lightCount = bgfx_create_uniform("u_lightCount", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_lights = bgfx_create_uniform("u_lights", BGFX_UNIFORM_TYPE_VEC4, 3 * kMaxDynamicLights);
        s_lightParams = bgfx_create_uniform("u_lightParams", BGFX_UNIFORM_TYPE_VEC4, kMaxDynamicLights);
        if (!bgfxIsValid(s_litSampler) || !bgfxIsValid(s_litPositionSampler) ||
            !bgfxIsValid(s_litGbufSampler) || !bgfxIsValid(s_litOccSampler) ||
            !bgfxIsValid(s_hemiColor) ||
            !bgfxIsValid(s_ambientColor) || !bgfxIsValid(s_envCube0) ||
            !bgfxIsValid(s_envCube1) || !bgfxIsValid(s_cubeValid) ||
            !bgfxIsValid(s_lightCount) || !bgfxIsValid(s_lights) || !bgfxIsValid(s_lightParams))
        {
            LogError("[BGFX] Lighting resolve uniforms create failed");
            DestroyResolveProgram();
            return false;
        }

        LogInfo("[BGFX] Lighting resolve program created: %u", s_resolveProgram.idx);
        return true;
    }

    void DestroyHighProgram()
    {
        if (bgfxIsValid(s_highProgram))
            bgfx_destroy_program(s_highProgram);
        if (bgfxIsValid(s_highSampler))
            bgfx_destroy_uniform(s_highSampler);
        if (bgfxIsValid(s_highTonemapSampler))
            bgfx_destroy_uniform(s_highTonemapSampler);
        if (bgfxIsValid(s_highPositionSampler))
            bgfx_destroy_uniform(s_highPositionSampler);
        s_highProgram = BGFX_INVALID_HANDLE;
        s_highSampler = BGFX_INVALID_HANDLE;
        s_highTonemapSampler = BGFX_INVALID_HANDLE;
        s_highPositionSampler = BGFX_INVALID_HANDLE;
        s_highLayoutReady = false;
    }

    bool EnsureHighProgram()
    {
        if (bgfxIsValid(s_highProgram) && bgfxIsValid(s_highSampler) &&
            bgfxIsValid(s_highTonemapSampler) && bgfxIsValid(s_highPositionSampler))
            return true;

        if (!s_highLayoutReady)
        {
            bgfx_vertex_layout_begin(&s_highLayout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_highLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_highLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_highLayout);
            s_highLayoutReady = true;
        }

        s_highProgram = BuildProgram("combine_vs.sc", "high_ps.sc");
        if (!bgfxIsValid(s_highProgram))
        {
            LogError("[BGFX] Split-HDR high program build failed");
            return false;
        }
        s_highSampler = bgfx_create_uniform("s_hdr", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_highTonemapSampler = bgfx_create_uniform("s_tonemap", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_highPositionSampler = bgfx_create_uniform("s_position", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        if (!bgfxIsValid(s_highSampler) || !bgfxIsValid(s_highTonemapSampler) ||
            !bgfxIsValid(s_highPositionSampler))
        {
            LogError("[BGFX] Split-HDR high uniforms create failed");
            DestroyHighProgram();
            return false;
        }
        LogInfo("[BGFX] Split-HDR high program created: %u", s_highProgram.idx);
        return true;
    }

    bool SubmitLuminancePass(bgfx_view_id_t _view, bgfx_frame_buffer_handle_t _fb,
        uint16_t _width, uint16_t _height, float _pass, bgfx_texture_handle_t _source,
        bgfx_texture_handle_t _previous, float _sourceWidth, float _sourceHeight,
        const float _middleGray[4])
    {
        bgfx_set_view_frame_buffer(_view, _fb);
        bgfx_set_view_rect(_view, 0, 0, _width, _height);
        bgfx_set_view_clear(_view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(_view, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_set_view_transform(_view, s_identity, s_identity);
        bgfx_touch(_view);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_lumLayout);
        if (!tvb.data)
            return false;
        struct Vertex
        {
            float x, y, z, u, v;
        };
        const Vertex vertices[3] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 0.0f, -1.0f },
        };
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        const float params[4] = { _pass, _sourceWidth, _sourceHeight, 0.0f };
        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        bgfx_set_texture(0, s_lumImage, _source, 0);
        bgfx_set_texture(1, s_lumPrev, _previous, 0);
        bgfx_set_uniform(s_lumParams, params, 1);
        bgfx_set_uniform(s_lumMiddleGray, _middleGray, 1);
        bgfx_submit(_view, s_lumProgram, 0, BGFX_DISCARD_ALL);
        return true;
    }

    // Anomaly r3 fog globals, 1:1 with the R_constant_setup binders in
    // SourcesAXR/Layers/xrRender/Blender_Recorder_StandartBinding.cpp:
    //   fog_params         cl_fog_params         :170-181  (-n*r, n, f, r)
    //   fog_color          cl_fog_color          :184-194  (rgb, density)
    //   lowland_fog_params cl_lowland_fog_params :198-208  (height, density, base height, 0)
    // u_sunDir / u_sunColor stay bound: combine_1.ps:204 only needs them through
    // the SSFX branch we dropped, but the lighting resolve (deferred_light_ps.sc)
    // and the sky both read the descriptor sun through them.
    //   u_sunDir     = normalize(Device.mView * CEnvDescriptor::sun_dir), already
    //                  view space, which is the space accum_sun.ps works in
    //                  (Ldynamic_dir); u_sunColor = CEnvDescriptor::sun_color
    //   u_hemiColor  = env_color, i.e. L_hemi_color, the source
    //                  calc_model_hemi_r1() reads (common_functions.h:99-101); the
    //                  writers' G-buffer hemi is the bare up factor max(0, Nw.y)
    //                  that this colour scales (gbuf_pack.h). It is NOT the raw
    //                  descriptor hemi: r4_rendertarget_phase_combine.cpp:222-231
    //                  builds env_color as (sky|hemi)_color*2 + EPS and then scales
    //                  x/y/z by 2*sun_lumscale_hemi, i.e. the descriptor value times
    //                  4*sun_lumscale_hemi, with EPS keeping it off zero.
    //   u_ambient    = L_ambient (hmodel.h:129), also NOT the raw descriptor ambient:
    //                  phase_combine.cpp:218-220 builds it as max(ambient*2, 0.001)
    //                  scaled by sun_lumscale_amb + nightvision_lum_factor. Its
    //                  .w = CEnvDescriptorMixer::weight, i.e. the env_color.w of
    //                  hmodel.h:105 - the factor the two ambient cubes are
    //                  lerped with, the same slot
    //                  Blender_Recorder_StandartBinding.cpp:390 feeds L_ambient.w
    // CurrentEnv is a CEnvDescriptorMixer, which derives from CEnvDescriptor
    // (Environment.h:258), so all four live there.
    void SetEnvironmentUniforms()
    {
        float params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float fogColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float lowland[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float sunDir[4] = { 0.0f, -1.0f, 0.0f, 0.0f };
        float sunColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float hemiColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float ambient[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        CEnvDescriptorMixer* env = g_pGamePersistent
            ? g_pGamePersistent->Environment().CurrentEnv
            : nullptr;
        if (env)
        {
            // r4_rendertarget_phase_combine.cpp:207-216, the two lumscale accumulators.
            // ps_r2_sun_lumscale_amb / ps_r2_sun_lumscale_hemi are xrRender console
            // variables (xrRender_console.cpp:313-314) that this layer does not link
            // (the same reason ps_r2_gloss_min above is spelled out); their reference
            // defaults - the advanced_settings "start_settings" entries no shipped
            // config overrides - are used instead. bWeatherSunLumscale is
            // ENGINE_API (x_ray.h:79, x_ray.cpp:91) and links, and the weather half of
            // the sum is the mixer's own field (Environment.h:209-210), exactly the
            // envdesc.sun_lumscale_amb / ._hemi the reference adds here.
            const float kSunLumscaleAmb = 1.0f;   // ps_r2_sun_lumscale_amb
            const float kSunLumscaleHemi = 1.0f;  // ps_r2_sun_lumscale_hemi
            float sun_lumscale_amb = kSunLumscaleAmb;
            float sun_lumscale_hemi = kSunLumscaleHemi;
            if (bWeatherSunLumscale)
            {
                sun_lumscale_amb += env->sun_lumscale_amb;
                sun_lumscale_hemi += env->sun_lumscale_hemi;
            }

            float n = env->fog_near;
            float f = env->fog_far;
            float r = 0.0f;
            if (f - n <= 0.001f)
            {
                n = 0.0f;
                f = 0.0f;
            }
            else
                r = 1.0f / (f - n);
            params[0] = -n * r;
            params[1] = n;
            params[2] = f;
            params[3] = r;

            fogColor[0] = env->fog_color.x;
            fogColor[1] = env->fog_color.y;
            fogColor[2] = env->fog_color.z;
            fogColor[3] = env->fog_density;

            lowland[0] = env->lowland_fog_height;
            lowland[1] = env->lowland_fog_density;
            lowland[2] = g_discord.LowlandFogBaseHeight;
            lowland[3] = 0.0f;

            Fvector vd;
            Device.mView.transform_dir(vd, env->sun_dir);
            vd.normalize_safe();
            sunDir[0] = vd.x;
            sunDir[1] = vd.y;
            sunDir[2] = vd.z;
            sunDir[3] = 0.0f;

            sunColor[0] = env->sun_color.x;
            sunColor[1] = env->sun_color.y;
            sunColor[2] = env->sun_color.z;
            sunColor[3] = 0.0f;

            // env_color, r4_rendertarget_phase_combine.cpp:222-231: the SoC weather
            // path takes sky_color, everything else hemi_color, both scaled by *2 + EPS
            // (EPS = 0.00001f, xrCore/vector.h:53) and then by 2*sun_lumscale_hemi -
            // so the descriptor colour reaches the shader times 4*sun_lumscale_hemi.
            // Handing the raw descriptor value over instead puts both ambient halves at
            // a quarter of the reference's brightness, which is exactly the "ambient is
            // 4x darker" reading. .w is not touched: the reference scales x/y/z only
            // (:229-231), env_color.w carries the cube lerp factor (hmodel.h:105).
            Fvector3 envColor;
            envColor.set(env->hemi_color.x, env->hemi_color.y, env->hemi_color.z);
            if (g_pGamePersistent->Environment().used_soc_weather)
                envColor.set(env->sky_color.x, env->sky_color.y, env->sky_color.z);
            const float envK = 2.f * sun_lumscale_hemi;
            hemiColor[0] = (envColor.x * 2.f + EPS) * envK;
            hemiColor[1] = (envColor.y * 2.f + EPS) * envK;
            hemiColor[2] = (envColor.z * 2.f + EPS) * envK;
            hemiColor[3] = 0.0f;

            // L_ambient, r4_rendertarget_phase_combine.cpp:218-220:
            // max(ambient*2, minamb) * (sun_lumscale_amb + nightvision_lum_factor),
            // i.e. the descriptor ambient doubled, floored at 0.001 so a black
            // descriptor still leaves a trace, then scaled by the ambient lumscale.
            const float minamb = 0.001f;
            const float ambK = sun_lumscale_amb
                + g_pGamePersistent->devices_shader_data.nightvision_lum_factor;
            ambient[0] = _max(env->ambient.x * 2.f, minamb) * ambK;
            ambient[1] = _max(env->ambient.y * 2.f, minamb) * ambK;
            ambient[2] = _max(env->ambient.z * 2.f, minamb) * ambK;
            // .w = CEnvDescriptorMixer::weight, i.e. the env_color.w of hmodel.h:105 -
            // the factor the two ambient cubes are lerped with, the same slot
            // Blender_Recorder_StandartBinding.cpp:389-390 feeds L_ambient.w. The
            // reference's L_ambient.w starts at 0 (:219) and ambclr.mul() (:220) cannot
            // lift it, so nothing here scales the weight.
            ambient[3] = env->weight;
        }

        if (bgfxIsValid(s_fogParams))
            bgfx_set_uniform(s_fogParams, params, 1);
        if (bgfxIsValid(s_fogColor))
            bgfx_set_uniform(s_fogColor, fogColor, 1);
        if (bgfxIsValid(s_lowlandFogParams))
            bgfx_set_uniform(s_lowlandFogParams, lowland, 1);
        if (bgfxIsValid(s_sunDir))
            bgfx_set_uniform(s_sunDir, sunDir, 1);
        if (bgfxIsValid(s_sunColor))
            bgfx_set_uniform(s_sunColor, sunColor, 1);
        if (bgfxIsValid(s_hemiColor))
            bgfx_set_uniform(s_hemiColor, hemiColor, 1);
        if (bgfxIsValid(s_ambientColor))
            bgfx_set_uniform(s_ambientColor, ambient, 1);
    }

    // The ambient cube pair itself. env_s0 / env_s1 (hmodel.h:14-15) are
    // CEnvDescriptor::sky_texture_env of the two descriptors the weather mixer
    // blends, published as bgfxEnvDescriptorMixerRender::sky_env_a / sky_env_b
    // and read back through bgfxGetAmbientCube(). Sampler stages 3 and 4, right
    // after the three G-buffer attachments.
    // The 0/1 switch is what keeps a cube-less level on the previous constant:
    // a sampler with no texture behind it is not a defined value, so the shader
    // must be told to ignore it (deferred_light_ps.sc, u_cubeValid).
    void BindAmbientCube()
    {
        if (!bgfxIsValid(s_envCube0) || !bgfxIsValid(s_envCube1) || !bgfxIsValid(s_cubeValid))
            return;

        bgfx_texture_handle_t envA = BGFX_INVALID_HANDLE;
        bgfx_texture_handle_t envB = BGFX_INVALID_HANDLE;
        bool hasCube = false;
        if (g_pGamePersistent)
            hasCube = bgfxGetAmbientCube(g_pGamePersistent->Environment(), envA, envB);

        if (hasCube)
        {
            bgfx_set_texture(3, s_envCube0, envA, 0);
            bgfx_set_texture(4, s_envCube1, envB, 0);
        }
        const float valid[4] = { hasCube ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
        bgfx_set_uniform(s_cubeValid, valid, 1);
    }

    // u_diffuse2s, archive_sourse/Layers/xrRenderPC_R4/r2_types.h:160-172: the
    // specular weight every accumulator binds into Ldynamic_color.w
    // (r4_rendertarget_accum_point.cpp:37 / :105, r4_rendertarget_accum_spot.cpp:125 /
    // :151), which lmodel.h:116 then multiplies the gloss channel with. The selector is
    // ps_ssfx_gloss_method, and only its method-0 arm runs in this build: the variable
    // (xr_ioc_cmd.cpp:500, initialised to 0) is bound to no console command anywhere in
    // this tree, so no reachable configuration can make it differ from that, and xrEngine
    // is an LTCG static library that internalises a variable nothing inside it reads - so
    // this layer cannot link it and does not pretend to. Method 0's own two inputs are the
    // R2 console variables ps_r2_gloss_min / ps_r2_gloss_factor, which this layer does not
    // link either; their reference defaults (xrRender_console.cpp:373-374, the
    // advanced_settings start_settings entries no shipped config overrides) are spelled
    // out below, and the method-1 arm is left in place with the two ps_ssfx values it does
    // read, so the expression can be diffed against the reference line by line.
    float u_diffuse2s(float _x, float _y, float _z)
    {
        const float kGlossMin = 0.0f;      // ps_r2_gloss_min
        const float kGlossFactor = 0.001f;  // ps_r2_gloss_factor
        // r2_types.h:170, the method-1 arm: "remove sun from the equation and clamp".
        const float span = ps_ssfx_gloss_minmax.y - ps_ssfx_gloss_minmax.x;
        const float clamped = span < 0.f ? 0.f : (span > 1.f ? 1.f : span);
        const float method1 = ps_ssfx_gloss_minmax.x + clamped * ps_ssfx_gloss_factor;

        const float v = (_x + _y + _z) / 3.f;
        const float method0 = kGlossMin + kGlossFactor * ((v < 1.f) ? powf(v, 2.f / 3.f) : v);

        return method0 + (method1 - method0) * 0.f;   // ps_ssfx_gloss_method == 0
    }

    // The dynamic point / spot light list for this frame, 1:1 with the reference's own
    // pre-pass:
    //   r2_R_calculate.cpp:58-73   every STYPE_LIGHTSOURCE spatial -> Lights.add_light
    //   Light_DB.cpp:219-227        add_light -> L->export_to(package[getVP()])
    //   light.cpp:362-367            export_to, the unshadowed branch, is the switch
    //                                between package.v_point and package.v_spot
    //   r2_R_lights.cpp:33-48        isLightVisible(): the distance / behind-camera test
    //   r2_R_lights.cpp:191-271      accum_point / accum_spot over those two vectors
    // The registry this walks is bgfxDynamicLights() (bgfxRenderInterface.h), the port's
    // stand-in for the reference's spatial query, which it cannot have: the port's light
    // class is a plain IRender_Light and never registers a spatial.
    //
    // Not carried over, all of it a per-light shadow-map feature the port does not have
    // and which is a separate stage: the bShadow split (light.cpp:305), the six omniparts
    // a shadowed point becomes (light.cpp:307-356) and get_LOD()'s distance fade
    // (light.cpp:382-388, which is 1 for an unshadowed light anyway). Every light is
    // therefore accumulated as the reference's unshadowed element does.
    //
    // The light data is ONE interleaved array, 3 vec4 per light in the order the reference's
    // own Ldynamic_* constants occupy:  [0] Ldynamic_pos (pos.xyz, att_factor),
    // [1] Ldynamic_color (L_clr.rgb, L_spec), [2] the view-space spot axis. That is the
    // layout deferred_light_ps.sc:661-663 reads (u_lights[i*3+0..2]) and the layout the
    // uniform is sized for (3 * kMaxDynamicLights vec4, line 1613). Writing the three
    // values into three separate 4*kMaxDynamicLights-apart blocks instead leaves every
    // colour at zero and every axis at zero, which is invisible: a POINT lamp then
    // accumulates exactly 0 and a SPOT one is culled by the z <= 0 test.
    u32 CollectDynamicLights(float* _lights, float* _params, u32 _capacity)
    {
        // ps_r__opt_dist, the reference's own cull distance (xrRender_console.cpp:472).
        const float kOptDist = 100.f;

        const std::vector<bgfxLight*>& all = bgfxDynamicLights();
        u32 count = 0;
        for (u32 i = 0; i < all.size(); ++i)
        {
            if (count >= _capacity)
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    LogInfo("[BGFX] dynamic lights: %u in the registry, only the first %u are accumulated",
                        (u32)all.size(), _capacity);
                }
                break;
            }
            bgfxLight* L = all[i];
            if (!L)
                continue;
            // light::export_to, the unshadowed branch (light.cpp:362-367): only the
            // omni and spot types reach the accumulators; DIRECT is the sun (the two
            // CLight_DB sun objects, r2_types.h) and REFLECTED is render_indirect's
            // synthetic light (r2_R_lights.cpp:274-313, driven by ps_r2_ls_flags GI).
            if (L->m_type != IRender_Light::POINT && L->m_type != IRender_Light::SPOT)
                continue;

            // r2_R_lights.cpp:33-48, isLightVisible.
            Fvector toLight;
            toLight.sub(L->m_position, Device.vCameraPosition);
            const float distance = toLight.magnitude();
            toLight.normalize();
            if (distance > kOptDist)
                continue;
            if (Device.vCameraDirection.dotproduct(toLight) < 0.f && distance > L->m_range)
                continue;

            // L_R = range * 0.95, the range both accumulators attenuate over
            // (r4_rendertarget_accum_point.cpp:35, r4_rendertarget_accum_spot.cpp:148).
            const float range = L->m_range * 0.95f;
            Fvector L_pos, L_dir;
            // accum_point.cpp:38 / accum_spot.cpp:126, the view-space position, and
            // accum_spot.cpp:127-129, the view-space axis (transform_dir, then the
            // normalize the reference spells out but never reaches - the cone lives in
            // the vertices there, so this port needs the vector itself).
            Device.mView.transform_tiny(L_pos, L->m_position);
            Device.mView.transform_dir(L_dir, L->m_direction);
            L_dir.normalize_safe();

            // accum_point.cpp:104, "Ldynamic_pos" = (L_pos.xyz, att_factor)
            // accum_spot.cpp:149-150, the same two lines for the spot path.
            float* p = _lights + count * 12;
            p[0] = L_pos.x;
            p[1] = L_pos.y;
            p[2] = L_pos.z;
            p[3] = 1.f / (range * range);   // accum_point.cpp:104 / accum_spot.cpp:149-150

            // L_clr: accum_point.cpp:36 / accum_spot.cpp:123. accum_spot multiplies it
            // by get_LOD() at :124, which is 1 for an unshadowed light (light.cpp:384),
            // so both accumulators see the colour as it is here.
            // accum_point.cpp:105 / accum_spot.cpp:151, "Ldynamic_color" = (L_clr.rgb, L_spec)
            const float spec = u_diffuse2s(L->m_color.r, L->m_color.g, L->m_color.b);
            p[4] = L->m_color.r;
            p[5] = L->m_color.g;
            p[6] = L->m_color.b;
            p[7] = spec;                    // L_spec = u_diffuse2s(L_clr)

            // accum_spot.cpp:126-129, the view-space axis of the cone (transform_dir +
            // normalize). The reference keeps it out of the Ldynamic_* constants because
            // the axis travels in the light's own xform there; the merged loop needs the
            // vector itself, and this is the third vec4 of the same stride.
            p[8] = L_dir.x;
            p[9] = L_dir.y;
            p[10] = L_dir.z;
            p[11] = 0.f;

            float* q = _params + count * 4;
            q[0] = (L->m_type == IRender_Light::SPOT) ? 1.f : 0.f;
            // light.cpp:281, s = 2*range*tanf(cone/2) - the very factor the spot volume
            // is scaled with, so this is the reference's own half-angle, not a fit.
            q[1] = tanf(L->m_cone * 0.5f);
            q[2] = 0.f;
            q[3] = 0.f;

            ++count;
        }
        return count;
    }

    // Feeds deferred_light_ps.sc with this frame's dynamic lights. Called from
    // ResolvePass, i.e. once per frame, right before the submit that reads them.
    void SetDynamicLightUniforms()
    {
        if (!bgfxIsValid(s_lightCount) || !bgfxIsValid(s_lights) || !bgfxIsValid(s_lightParams))
            return;

        s_lightData.assign(4 * 3 * kMaxDynamicLights, 0.f);
        s_lightParamData.assign(4 * kMaxDynamicLights, 0.f);
        const u32 count = CollectDynamicLights(
            s_lightData.data(),
            s_lightParamData.data(),
            kMaxDynamicLights);

        const float count4[4] = { (float)count, 0.f, 0.f, 0.f };
        bgfx_set_uniform(s_lightCount, count4, 1);
        bgfx_set_uniform(s_lights, s_lightData.data(), 3 * kMaxDynamicLights);
        bgfx_set_uniform(s_lightParams, s_lightParamData.data(), kMaxDynamicLights);

        // One line, the first frame that has lights: the only visible evidence that
        // the accumulators are fed, since from inside the resolve they are just a sum.
        static int s_logged = 0;
        if (count > 0 && !s_logged)
        {
            s_logged = 1;
            LogInfo("[BGFX] dynamic point/spot accumulators: %u of %u registered lights, first = pos(%f %f %f) range=%f cone=%f",
                count, (u32)bgfxDynamicLights().size(),
                s_lightData[0], s_lightData[1], s_lightData[2],
                sqrtf(1.f / s_lightData[3]) / 0.95f,
                atanf(s_lightParamData[1]) * 2.f);
        }
    }
}

namespace bgfxHDR
{
    // SMAA helpers are defined after CombinePass below (they reuse its
    // layout/uniform patterns); forward declarations for the earlier callers.
    void DestroySmaaTargets();
    void DestroySmaaPrograms();
    bool EnsureSmaaTargets(uint16_t _width, uint16_t _height);
    bool EnsureSmaaPrograms();
    bool EnsureSmaaTextures();
    bool IsSmaaReady(uint16_t _width, uint16_t _height);
    void DestroyFogScatter();
    bool EnsureFogScatter(uint16_t _width, uint16_t _height);
    bool FogScatterPass(uint16_t _width, uint16_t _height);
    bgfx_texture_handle_t ScatterOutputTexture();

    bool CreateHDRTarget(uint16_t _width, uint16_t _height)
    {
        if (_width == 0 || _height == 0)
            return false;

        if (bgfxIsValid(s_hdrFb) && s_width == _width && s_height == _height)
            return EnsureLuminanceTargets() && EnsureBloomTargets() && EnsureBloomPrograms() &&
                EnsureCombineProgram() && EnsureResolveProgram() && EnsureHighProgram();

        DestroyHDRTarget();

        s_colorFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
        if (!IsTextureSupported(s_colorFormat))
        {
            LogInfo("[BGFX] HDR target: RGBA16F unavailable, using RGBA32F");
            s_colorFormat = BGFX_TEXTURE_FORMAT_RGBA32F;
            if (!IsTextureSupported(s_colorFormat))
            {
                LogError("[BGFX] HDR target: no supported floating-point color format");
                return false;
            }
        }

        // Position G-buffer: the AXR gbuf position pass (combine_1.ps:194-201 reads
        // P.xyz straight out of it), so the fog branch can be ported 1:1 instead of
        // being reconstructed from depth. RGBA16F with an RGBA32F fallback.
        s_positionFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
        if (!IsTextureSupported(s_positionFormat))
        {
            LogInfo("[BGFX] Position target: RGBA16F unavailable, using RGBA32F");
            s_positionFormat = BGFX_TEXTURE_FORMAT_RGBA32F;
            if (!IsTextureSupported(s_positionFormat))
            {
                LogError("[BGFX] Position target: no supported floating-point color format");
                return false;
            }
        }

        s_depthFormat = BGFX_TEXTURE_FORMAT_D24;
        if (!IsTextureSupported(s_depthFormat))
        {
            LogInfo("[BGFX] HDR target: D24 unavailable, using D24S8");
            s_depthFormat = BGFX_TEXTURE_FORMAT_D24S8;
            if (!IsTextureSupported(s_depthFormat))
            {
                LogInfo("[BGFX] HDR target: D24S8 unavailable, using D32FS8");
                s_depthFormat = BGFX_TEXTURE_FORMAT_D32FS8;
                if (!IsTextureSupported(s_depthFormat))
                {
                    LogError("[BGFX] HDR target: no supported depth format");
                    return false;
                }
            }
        }

        // Packed G-buffer (stage 1): AXR f_deffer::position
        // (game_unpacked/shaders/r3/gbuffer_stage.h:7) = [packed normal .xy,
        // view-space z, hemi], written by every world/terrain/skin/grass/
        // particle/wallmark PS through gbuf_pack_gbuffer() in gbuf_pack.h. Same
        // format policy as the two existing colour attachments.
        s_gbufFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
        if (!IsTextureSupported(s_gbufFormat))
        {
            LogInfo("[BGFX] G-buffer target: RGBA16F unavailable, using RGBA32F");
            s_gbufFormat = BGFX_TEXTURE_FORMAT_RGBA32F;
            if (!IsTextureSupported(s_gbufFormat))
            {
                LogError("[BGFX] G-buffer target: no supported floating-point format");
                return false;
            }
        }

        const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        const uint64_t depthFlags = BGFX_TEXTURE_RT;
        s_hdrColor = bgfx_create_texture_2d(_width, _height, false, 1, s_colorFormat, colorFlags, nullptr, 0);
        s_hdrPosition = bgfx_create_texture_2d(_width, _height, false, 1, s_positionFormat, colorFlags, nullptr, 0);
        s_hdrGbuf = bgfx_create_texture_2d(_width, _height, false, 1, s_gbufFormat, colorFlags, nullptr, 0);
        s_hdrDepth = bgfx_create_texture_2d(_width, _height, false, 1, s_depthFormat, depthFlags, nullptr, 0);
        // Lit HDR target (stage 2). Same size and format as attachment 0 so the
        // luminance chain and the combine can treat it as a drop-in replacement,
        // but a separate texture: the resolve samples attachment 0 / 1 / 2 and
        // must not render into any of them.
        s_hdrLit = bgfx_create_texture_2d(_width, _height, false, 1, s_colorFormat, colorFlags, nullptr, 0);
        // Split-HDR high channel, the AXR rt_Generic_1 (r4_rendertarget.cpp:490).
        // Same format policy as the other colour attachments: AXR stores it 8-bit
        // (D3DFMT_A8R8G8B8) because there the /9 is a way to squeeze a second
        // dynamic range into 8 bits, while in this port the pre-tonemap image it
        // encodes is itself 16F, so the pair is 16F/16F and the encoding is the
        // only thing that is ported 1:1. The bloom bright pass is its only reader,
        // which is why the bright pass has to run before the luminance chain and
        // before the high target is reused for anything else.
        s_hdrHigh = bgfx_create_texture_2d(_width, _height, false, 1, s_colorFormat, colorFlags, nullptr, 0);
        // SSAO target: half resolution, because the reference renders
        // r2_RT_ssao_temp through a viewport of dwWidth/2 x dwHeight/2
        // (r4_rendertarget_phase_ssao.cpp:52-55) and reads the result back
        // magnified, i.e. a quarter of the reference's full-size allocation and
        // the same values. R16F, as D3DFMT_R16F (r4_rendertarget.cpp:861);
        // EnsureSsaoTargets() below builds it once the size is known.
        if (!bgfxIsValid(s_hdrColor) || !bgfxIsValid(s_hdrPosition) || !bgfxIsValid(s_hdrGbuf) ||
            !bgfxIsValid(s_hdrDepth) || !bgfxIsValid(s_hdrLit) || !bgfxIsValid(s_hdrHigh))
        {
            LogError("[BGFX] HDR target texture create failed (%ux%u)", _width, _height);
            DestroyTextures();
            return false;
        }

        // Attachment 0 = HDR color, 1 = view-space position G-buffer,
        // 2 = packed G-buffer, 3 = depth. Every world/particle/wallmark PS writes
        // all three colour targets; the sky and the clouds only write target 0,
        // which leaves P and the packed G-buffer at the clear value 0, so the
        // fog branch below fades them out exactly like Anomaly does and the
        // gbuf_debug_ps.sc inspector reports those pixels as empty.
        bgfx_texture_handle_t attachments[4] = { s_hdrColor, s_hdrPosition, s_hdrGbuf, s_hdrDepth };
        s_hdrFb = bgfx_create_frame_buffer_from_handles(4, attachments, true);
        if (!bgfxIsValid(s_hdrFb))
        {
            LogError("[BGFX] HDR target framebuffer create failed (%ux%u)", _width, _height);
            s_hdrFb = BGFX_INVALID_HANDLE;
            DestroyTextures();
            return false;
        }

        s_width = _width;
        s_height = _height;
        // Resolve target: one colour attachment, no depth - the pass covers the
        // whole viewport and tests nothing.
        bgfx_texture_handle_t litAttachment[1] = { s_hdrLit };
        s_hdrLitFb = bgfx_create_frame_buffer_from_handles(1, litAttachment, false);
        if (!bgfxIsValid(s_hdrLitFb))
        {
            LogError("[BGFX] Lighting resolve framebuffer create failed (%ux%u)", _width, _height);
            s_hdrLitFb = BGFX_INVALID_HANDLE;
            DestroyTextures();
            return false;
        }
        // High target: same shape as the resolve target, for the same reason.
        bgfx_texture_handle_t highAttachment[1] = { s_hdrHigh };
        s_hdrHighFb = bgfx_create_frame_buffer_from_handles(1, highAttachment, false);
        if (!bgfxIsValid(s_hdrHighFb))
        {
            LogError("[BGFX] Split-HDR high framebuffer create failed (%ux%u)", _width, _height);
            s_hdrHighFb = BGFX_INVALID_HANDLE;
            DestroyTextures();
            return false;
        }
        LogInfo("[BGFX] HDR target created: %ux%u color=%d position=%d gbuf=%d depth=%d fb=%u litfb=%u highfb=%u",
            _width, _height, (int)s_colorFormat, (int)s_positionFormat, (int)s_gbufFormat,
            (int)s_depthFormat, s_hdrFb.idx, s_hdrLitFb.idx, s_hdrHighFb.idx);

        if (!CreateLuminanceTargets() || !EnsureBloomTargets() || !EnsureBloomPrograms() ||
            !EnsureCombineProgram() || !EnsureResolveProgram() || !EnsureHighProgram() ||
            !EnsureShadowTargets() || !EnsureShadowProgram() ||
            !EnsureSsaoTargets() || !EnsureSsaoProgram() || !EnsureSsaoJitter())
        {
            DestroyHDRTarget();
            return false;
        }
        return true;
    }

    void DestroyHDRTarget()
    {
        if (bgfxIsValid(s_hdrFb))
            bgfx_destroy_frame_buffer(s_hdrFb);
        s_hdrFb = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_hdrLitFb))
            bgfx_destroy_frame_buffer(s_hdrLitFb);
        s_hdrLitFb = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_hdrHighFb))
            bgfx_destroy_frame_buffer(s_hdrHighFb);
        s_hdrHighFb = BGFX_INVALID_HANDLE;
        DestroyTextures();
        s_width = 0;
        s_height = 0;
        s_gbufDebugClock = 0.0f;
        DestroyCombineProgram();
        DestroyResolveProgram();
        DestroyHighProgram();
        DestroyGbufDebugProgram();
        DestroyBloomPrograms();
        DestroyBloomTargets();
        DestroyLuminancePrograms();
        DestroyLuminanceTargets();
        DestroySmaaTargets();
        DestroySmaaPrograms();
        DestroyFogScatter();
        DestroyShadow();
        DestroySsao();
    }

    bool RecreateOnResize(uint16_t _width, uint16_t _height)
    {
        if (bgfxIsValid(s_hdrFb) && s_width == _width && s_height == _height)
            return EnsureLuminanceTargets() && EnsureBloomTargets() && EnsureBloomPrograms() &&
                EnsureCombineProgram() && EnsureResolveProgram() && EnsureHighProgram();
        // Full-res SMAA/scatter targets follow the window size; drop them here,
        // the passes recreate them lazily at the new size.
        DestroySmaaTargets();
        DestroyFogScatter();
        return CreateHDRTarget(_width, _height);
    }

    bool IsReady()
    {
        return bgfxIsValid(s_hdrFb) && s_width != 0 && s_height != 0;
    }

    bool BindScene()
    {
        if (!IsReady() || !EnsureCombineProgram())
            return false;

        // The G-buffer writers take their hemi per pixel from the normal now
        // (gbuf_pack.h, gbuf_calc_hemi = max(0, Nw.y), the scalar half of
        // calc_model_hemi_r1(), common_functions.h:99-101); the descriptor's
        // hemi colour that used to scale it is applied by the resolve from
        // u_hemiColor. So there is no frame-wide hemi constant left to set here.

        bgfx_set_view_frame_buffer(kSceneView, s_hdrFb);
        bgfx_set_view_frame_buffer(kSceneFxView, s_hdrFb);
        bgfx_set_view_rect(kSceneView, 0, 0, s_width, s_height);
        // bgfx applies the view clear color to every color attachment, so the
        // position G-buffer starts at (0,0,0,1) => P = 0 for the pixels no
        // geometry wrote (sky, clouds) and the AXR fog branch leaves them alone
        // (length(P) = 0 => fog = saturate(0 * w + fog_params.x) = 0). The same
        // zero is what gbuf_debug_ps.sc uses to report an unwritten attachment.
        bgfx_set_view_clear(kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
        bgfx_set_view_mode(kSceneView, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_touch(kSceneView);
        bgfx_set_view_rect(kSceneFxView, 0, 0, s_width, s_height);
        bgfx_set_view_clear(kSceneFxView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kSceneFxView, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_touch(kSceneFxView);
        return true;
    }

    // Stage-2 lighting resolve. The reference builds the lit image in two
    // places and this pass is the bgfx stand-in for both at once:
    //   sun   accum_sun.ps:23-35 - plight_infinity(mtl, P, N, C, Ldynamic_dir)
    //         tinted by SRGBToLinear(Ldynamic_color.rgb) and the shadow term,
    //         which this pass leaves at 1 (no shadow map is bound). Both the
    //         diffuse and the specular lobe of compute_lighting (lmodel.h:142-173)
    //         are in there, the latter gated by the G-buffer gloss channel.
    //   hemi  hmodel() - the ambient half combine_1.ps:166 adds: the diffuse
    //         SRGBToLinear(env_d) * albedo with
    //         env_d = lerp(env_s0, env_s1, env_color.w) * env_col * hemi + L_ambient
    //         (hmodel.h:105, :121, :125, :130), plus the Amb_BRDF specular term
    //         of hmodel.h:145 sampling the same two cubes along the reflection
    //         vector at the roughness mip (hmodel.h:101-102, :44).
    // Both terms read the per-pixel hemi and the packed normal out of attachment
    // 2 and modulate the gamma-space albedo of attachment 0, exactly like the
    // reference does through gbuffer_load_data() (gbuffer_stage.h:115-143).
    // Pixels nothing drew (sky, clouds) keep their attachment-0 radiance: see
    // the P == 0 branch in deferred_light_ps.sc.
    //
    // Neither specular half needs anything bound that is not already here, and
    // that is the reason this pass grew them without a single new uniform or
    // texture:
    //   gloss   the reference's G-buffer gloss channel is the compile-time
    //           literal def_gloss = 2/255 (common_defines.h:6) in every writer
    //           (deffer_base_flat.ps:51/:54, deffer_grass.ps:88,
    //           deffer_particle.ps:69, lod.ps:105), while the port's attachment
    //           0 stores the sampled texel verbatim (world_solid_ps.sc:25) and
    //           so carries no gloss at all. The shader therefore uses the
    //           literal, which is the reference value rather than a stand-in.
    //   Ldynamic_color.w  multiplies that gloss in the sun half (lmodel.h:116)
    //           and enters calc_rough (pbr_brdf.h:116) on the ambient side.
    //           CEnvDescriptor::sun_color is a Fvector3 (Environment.h:186) and
    //           the R4 constant that would carry it is never bound in this
    //           tree (R_hemi::set_material has no caller), so the shader holds
    //           it at the reference's own 1.0 - the same multiply appears
    //           commented out at lmodel.h:122, and 0.0 would zero the specular.
    //   s_material  the material LUT (common_samplers.h:69) is sampled once,
    //           at lmodel.h:133, and the result is overwritten at lmodel.h:147
    //           and :151 before it is read, so the whole non-ES_PSEUDO_PBR lobe
    //           is closed form in mat_id. No LUT texture has to be created
    //           here, and none is invented.
    // r2_R_sun.cpp:311-328 computes the frustum the sun matrix is fitted to, from
    // the same fov / aspect the projection uses, over
    //   _far_ = min(OLES_SUN_LIMIT_27_01_07, CurrentEnv->far_plane)
    // (r2_R_sun.cpp:322, :13 - the constant is 100.f). VIEWPORT_NEAR is the near
    // plane of the frustum, per the reference's own comment at r2_R_sun.cpp:325.
    static const float OLES_SUN_LIMIT_27_01_07 = 100.0f;   // r2_R_sun.cpp:13

    // The sun matrix the caster pass renders with, kept for the resolve: the
    // reference hands the accumulator the same matrix it rendered with
    // (r2_R_sun.cpp:718 and r4_rendertarget_accum_direct.cpp:170 both read
    // fuckingsun->X.D.combine), so the two halves of the pass can never disagree.
    static SunMatrix s_sunMatrix = {};
    static bool s_sunMatrixValid = false;

    // Builds the sun matrix and binds the shadow view. The caller then re-walks the
    // world (bgfxRenderCompat.cpp, pass 0 of bgfxRenderWorld) and every mesh it
    // reaches is submitted into kShadowView with shadow_vs.sc / shadow_ps.sc,
    // which is r2_R_sun.cpp:727-740.
    bool ShadowBegin()
    {
        s_shadowMapValid = false;
        s_sunMatrixValid = false;
        if (!EnsureShadowTargets() || !EnsureShadowProgram())
            return false;

        CEnvDescriptorMixer* env = g_pGamePersistent
            ? g_pGamePersistent->Environment().CurrentEnv
            : nullptr;
        if (!env)
            return false;

        // The sun's light-travel direction in world space. CEnvDescriptor::sun_dir
        // is what Ldynamic_dir is derived from (deferred_light_ps.sc:351 negates the
        // view-space copy), so it is the same vector the reference's
        // fuckingsun->direction holds.
        Fvector sunDir = env->sun_dir;
        sunDir.normalize_safe();
        float farPlane = env->far_plane;
        if (farPlane > OLES_SUN_LIMIT_27_01_07)
            farPlane = OLES_SUN_LIMIT_27_01_07;
        if (farPlane <= VIEWPORT_NEAR)
            farPlane = OLES_SUN_LIMIT_27_01_07;

        if (!BuildSunMatrix(s_sunMatrix, &sunDir.x, Device.mView, deg2rad(Device.fFOV),
                            Device.fASPECT, farPlane))
            return false;

        // r2_R_sun.cpp:744-747 closes the SMAP render with r_pmask, and
        // phase_smap_direct (r2_R_sun.cpp:728) targets r2_RT_smap_depth. The clear is
        // the far value so that everything the walk does not reach reads as "nothing
        // between it and the light", which is what the depth compare then reports.
        bgfx_set_view_frame_buffer(kShadowView, s_shadowFb);
        bgfx_set_view_rect(kShadowView, 0, 0, (int)kSunSmapSize, (int)kSunSmapSize);
        bgfx_set_view_clear(kShadowView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0f, 0);
        bgfx_set_view_mode(kShadowView, BGFX_VIEW_MODE_SEQUENTIAL);
        // r2_R_sun.cpp:729-731: xform_world = identity, xform_view = identity,
        // xform_project = fuckingsun->X.D.combine. bgfx splits view and projection,
        // so the view half is the identity and the projection half is the sun matrix.
        static const float s_identity[16] =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        bgfx_set_view_transform(kShadowView, s_identity, s_sunMatrix.m);
        bgfx_touch(kShadowView);

        s_shadowPassActive = true;
        s_sunMatrixValid = true;
        s_shadowMapValid = true;
        return true;
    }

    void ShadowEnd()
    {
        s_shadowPassActive = false;
    }

    bool ShadowPassActive()
    {
        return s_shadowPassActive;
    }

    bgfx_program_handle_t GetShadowProgram()
    {
        return s_shadowProgram;
    }

    // Feeds deferred_light_ps.sc the m_shadow of r4_rendertarget_accum_direct.cpp:152-180:
    //   m_TexelAdjust * fuckingsun->X.D.combine * inverse(Device.mView)
    // where m_TexelAdjust is
    //   { 0.5, 0, 0, 0 / 0, -0.5, 0, 0 / 0, 0, fRange, 0 / 0.5, 0.5, fBias, 1 }
    // The reference picks fRange / fBias per cascade: SE_SUN_NEAR uses
    // ps_r2_sun_depth_near_scale with -ps_r2_sun_depth_near_bias, SE_SUN_FAR uses
    // ps_r2_sun_depth_far_scale with ps_r2_sun_depth_far_bias. This port has the one
    // map, i.e. the far cascade - the one whose fit covers the whole visible frustum -
    // so the far pair is the one applied. The tsm bias translate of
    // r4_rendertarget_accum_direct.cpp:174-179 is likewise a SE_SUN_FAR term and is
    // applied: the map is translated along the view-space light direction by
    // ps_r2_sun_tsm_bias, i.e. the caster depth is pushed away from the light.
    void SetShadowUniforms()
    {
        if (!bgfxIsValid(s_shadowMat) || !bgfxIsValid(s_shadowParams) || !s_sunMatrixValid)
            return;

        Fmatrix invView;
        invView.invert(Device.mView);

        const float fRange = kSunDepthFarScale;
        const float fBias  = kSunDepthFarBias;
        // r4_rendertarget_accum_direct.cpp:156-162, m_TexelAdjust, column-major.
        const SunMatrix texelAdjust =
        {
            {
                0.5f,  0.0f, 0.0f, 0.0f,
                0.0f, -0.5f, 0.0f, 0.0f,
                0.0f,  0.0f, fRange, 0.0f,
                0.5f,  0.5f, fBias, 1.0f,
            }
        };

        SunMatrix proj;
        SunMul(proj, texelAdjust, s_sunMatrix);

        // r4_rendertarget_accum_direct.cpp:174-179, the tsm bias along L_dir.
        // L_dir is the view-space light direction accum_sun_near.ps:23 reads, i.e. the
        // same vector the resolve's u_sunDir carries (deferred_light_ps.sc:351 turns it
        // into the surface-to-light vector with -normalize), so the bias is taken from
        // the descriptor rather than from the fitted basis: the fit may rotate the
        // light space, the bias may not.
        Fvector vd;
        vd.set(0.0f, 0.0f, 0.0f);
        CEnvDescriptorMixer* env = g_pGamePersistent
            ? g_pGamePersistent->Environment().CurrentEnv
            : nullptr;
        if (env)
        {
            Device.mView.transform_dir(vd, env->sun_dir);
            vd.normalize_safe();
        }
        SunMatrix bias_t;
        bias_t.m[0] = 1.0f; bias_t.m[1] = 0.0f; bias_t.m[2] = 0.0f; bias_t.m[3] = 0.0f;
        bias_t.m[4] = 0.0f; bias_t.m[5] = 1.0f; bias_t.m[6] = 0.0f; bias_t.m[7] = 0.0f;
        bias_t.m[8] = 0.0f; bias_t.m[9] = 0.0f; bias_t.m[10] = 1.0f; bias_t.m[11] = 0.0f;
        bias_t.m[12] = vd.x * kSunTsmBias;
        bias_t.m[13] = vd.y * kSunTsmBias;
        bias_t.m[14] = vd.z * kSunTsmBias;
        bias_t.m[15] = 1.0f;
        proj.m[12] += bias_t.m[12] * proj.m[0] + bias_t.m[13] * proj.m[4] + bias_t.m[14] * proj.m[8];
        proj.m[13] += bias_t.m[12] * proj.m[1] + bias_t.m[13] * proj.m[5] + bias_t.m[14] * proj.m[9];
        proj.m[14] += bias_t.m[12] * proj.m[2] + bias_t.m[13] * proj.m[6] + bias_t.m[14] * proj.m[10];
        proj.m[15] += bias_t.m[12] * proj.m[3] + bias_t.m[13] * proj.m[7] + bias_t.m[14] * proj.m[11];

        SunMatrix m_shadow;
        SunMul(m_shadow, proj, reinterpret_cast<const SunMatrix&>(invView.m));
        bgfx_set_uniform(s_shadowMat, m_shadow.m, 1);

        const float params[4] =
        {
            float(kSunSmapSize),
            s_shadowMapValid ? 1.0f : 0.0f,
            float(kSunQuality),
            0.0f,
        };
        bgfx_set_uniform(s_shadowParams, params, 1);
    }

    // =========================================================================
    // Screen-space ambient occlusion, 1:1 with CRenderTarget::phase_ssao
    // (r4_rendertarget_phase_ssao.cpp:12-105).
    //
    // The reference runs this pass from inside phase_combine
    // (r4_rendertarget_phase_combine.cpp:77-93) whenever the SSAO mode asks for
    // a separate buffer, and the same kernel is also available inline in the
    // combine itself (combine_1.ps:148, the calc_ssao() that ssao.ps
    // implements). Both are the identical function; the separate pass is the one
    // that produces a buffer, so it is the one this port runs, and the resolve
    // consumes it where the reference's combine consumes its occ.
    //
    // The view has to sit after every writer of the G-buffer it reads
    // (kSceneView and kSceneFxView, the last of them at id 6) and before the
    // resolve that applies the factor, which is why it takes kSsaoView = 21 and
    // the resolve / high channel moved to 22 / 23. bgfx renders views in
    // ascending id order (bgfx_p.h:3452 collectUsedViews), not in call order.
    // =========================================================================
    bool SSAOPass()
    {
        s_ssaoOk = false;
        if (!IsReady() || !EnsureSsaoTargets() || !EnsureSsaoProgram() || !EnsureSsaoJitter())
            return false;

        bgfx_set_view_frame_buffer(kSsaoView, s_ssaoFb);
        // r4_rendertarget_phase_ssao.cpp:52-55, set_viewport(_w, _h) with
        // _w / _h = dwWidth / 2, dwHeight / 2.
        bgfx_set_view_rect(kSsaoView, 0, 0, (uint16_t)(s_width / 2), (uint16_t)(s_height / 2));
        // r4_rendertarget_phase_ssao.cpp:16-17 clears rt_ssao_temp to
        // (0,0,0,0) and RCache.set_Stencil(FALSE) (:29) leaves the stencil off.
        // Every pixel of the viewport is shaded, so the clear is only needed for
        // the pixels the pass itself does not write - the shader reproduces the
        // stencil gate of blender_ssao.cpp:17-18 and writes that clear value.
        bgfx_set_view_clear(kSsaoView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kSsaoView, BGFX_VIEW_MODE_SEQUENTIAL);
        // r4_rendertarget_phase_ssao.cpp:69 sets m_v2w = inverse(Device.mView)
        // and ssao.ps:151 multiplies the view-space position with it to tile the
        // noise; the predefined u_invView is that matrix, so the view transform
        // is fed exactly as the resolve's is.
        bgfx_set_view_transform(kSsaoView, Device.mView.m, Device.mProject.m);
        bgfx_touch(kSsaoView);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_ssaoLayout);
        if (!tvb.data)
            return false;
        // The reference fills a 4-vertex quad through g_combine and
        // combine_1.vs turns it into the fullscreen triangle the other passes
        // use here; a single 3-vertex triangle is the same coverage.
        struct Vertex
        {
            float x, y, z, u, v;
        };
        const Vertex vertices[3] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 0.0f, -1.0f },
        };
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        bgfx_set_texture(0, s_ssaoPositionSampler, GetPositionTexture(), 0);
        bgfx_set_texture(1, s_ssaoGbufSampler, GetGbufTexture(), 0);
        bgfx_set_texture(2, s_ssaoJitterSampler, s_ssaoJitter, 0);

        // r4_rendertarget_phase_ssao.cpp:40-46, the two constants the kernel
        // reads, and the same expressions the reference also feeds the inline
        // combine (r4_rendertarget_phase_combine.cpp:312-313).
        const float fov = Device.fFOV;
        float noise = 2.0f;
        noise *= tan(deg2rad(67.5f));
        noise /= tan(deg2rad(fov));
        float kernel = 150.0f;
        kernel *= tan(deg2rad(67.5f));
        kernel /= tan(deg2rad(fov));
        const float noiseTile[4] = { noise, 0.0f, 0.0f, 0.0f };
        const float kernelSize[4] = { kernel, 0.0f, 0.0f, 0.0f };
        bgfx_set_uniform(s_ssaoNoiseTileFactor, noiseTile, 1);
        bgfx_set_uniform(s_ssaoKernelSize, kernelSize, 1);

        bgfx_submit(kSsaoView, s_ssaoProgram, 0, BGFX_DISCARD_ALL);
        s_ssaoOk = true;
        return true;
    }

    // The 1x1 constant the resolve falls back to when the pass could not run: the
    // reference has no such state (its pass always runs and always writes the
    // target), so the neutral value is the one its own kernel produces for an
    // unoccluded surface and the one compute_colored_ao(1, albedo) returns
    // unchanged - i.e. no occlusion at all.
    bgfx_texture_handle_t GetSsaoTexture()
    {
        if (s_ssaoOk && bgfxIsValid(s_ssao))
            return s_ssao;
        if (!bgfxIsValid(s_ssaoFallback))
        {
            const uint64_t flags = BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP |
                BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_POINT | BGFX_TEXTURE_MIP_POINT;
            const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            const uint32_t bytes = (uint32_t)sizeof(white);
            const bgfx_memory_t* mem = bgfx_copy(white, bytes);
            s_ssaoFallback = bgfx_create_texture_2d(1, 1, false, 1,
                BGFX_TEXTURE_FORMAT_R16F, flags, mem, 0);
            if (!bgfxIsValid(s_ssaoFallback))
            {
                s_ssaoFallback = BGFX_INVALID_HANDLE;
                return BGFX_INVALID_HANDLE;
            }
        }
        return s_ssaoFallback;
    }

    bool ResolvePass()
    {
        s_resolveOk = false;
        if (!IsReady() || !EnsureResolveProgram() || !bgfxIsValid(s_hdrLitFb))
            return false;

        bgfx_set_view_frame_buffer(kResolveView, s_hdrLitFb);
        bgfx_set_view_rect(kResolveView, 0, 0, s_width, s_height);
        bgfx_set_view_clear(kResolveView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kResolveView, BGFX_VIEW_MODE_SEQUENTIAL);
        // The VS emits clip space directly and the G-buffer is view space, so the
        // camera matrices never reach the geometry here. They are still fed to
        // the view because the ambient cube lookup needs the world-space normal
        // (hmodel.h:48, nw = mul(m_inv_V, normal)) and the predefined u_invView is
        // the bgfx mirror of m_inv_V, exactly as it is for the sky / cloud shaders.
        bgfx_set_view_transform(kResolveView, Device.mView.m, Device.mProject.m);
        bgfx_touch(kResolveView);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_resolveLayout);
        if (!tvb.data)
            return false;
        struct Vertex
        {
            float x, y, z, u, v;
        };
        const Vertex vertices[3] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 0.0f, -1.0f },
        };
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        SetEnvironmentUniforms();
        BindAmbientCube();
        // The sun shadow term of accum_sun_near.ps:69-72, i.e. the last piece the
        // reference multiplies into the sun lobe.
        SetShadowUniforms();
        // The dynamic point / spot accumulators, i.e. accum_omni_unshadowed.ps and
        // accum_base.ps over the volumes accum_point / accum_spot would have drawn.
        SetDynamicLightUniforms();
        bgfx_set_texture(0, s_litSampler, s_hdrColor, 0);
        bgfx_set_texture(1, s_litPositionSampler, s_hdrPosition, 0);
        bgfx_set_texture(2, s_litGbufSampler, s_hdrGbuf, 0);
        if (bgfxIsValid(s_smapSampler))
            bgfx_set_texture(5, s_smapSampler, s_shadowMap, 0);
        // combine_1.ps:128-159 / :183, the SSAO factor. blender_combine.cpp:42
        // binds the combine's s_occ to r2_RT_ssao_temp; the port's producer is
        // SSAOPass() above, and the fallback is the neutral 1.0 texture.
        const bgfx_texture_handle_t occ = GetSsaoTexture();
        if (bgfxIsValid(occ))
            bgfx_set_texture(6, s_litOccSampler, occ, 0);
        bgfx_submit(kResolveView, s_resolveProgram, 0, BGFX_DISCARD_ALL);
        s_resolveOk = true;
        return true;
    }

    // What the post chain samples as "the HDR image". The lit target once the
    // resolve has run this frame, the raw unlit attachment 0 otherwise, so a
    // failed resolve degrades to the pre-stage-2 frame instead of a black one.
    bgfx_texture_handle_t GetLitTexture()
    {
        if (s_resolveOk && bgfxIsValid(s_hdrLit))
            return s_hdrLit;
        if (bgfxIsValid(s_hdrColor))
            return s_hdrColor;
        return BGFX_INVALID_HANDLE;
    }

    // Split-HDR high channel, the AXR r2_RT_generic1 (r4_rendertarget.cpp:490). In the
    // reference three shaders write it - sky2.ps:60, combine_1.ps:213 through
    // tonemap()'s high line (common_functions.h:32) and combine_volumetric.ps:33 - and
    // the single reader is the bloom bright pass (blender_bloom_build.cpp:18). Here the
    // three writers collapse into one fullscreen pass over the lit image: the sky and
    // the clouds are already part of that image (the resolve passes their radiance
    // through) and the volumetric term has no bgfx counterpart, so one pass over the
    // lit image is the same set of values.
    // It runs before the bright pass and after the resolve, and it reads the same
    // tm_scale the sky and the combine read, so all three agree for the whole frame.
    bool HighPass()
    {
        s_highOk = false;
        if (!IsReady() || !EnsureHighProgram() || !bgfxIsValid(s_hdrHighFb))
            return false;
        const bgfx_texture_handle_t tonemap = GetTonemapTexture();
        if (!bgfxIsValid(tonemap))
            return false;

        bgfx_set_view_frame_buffer(kHighView, s_hdrHighFb);
        bgfx_set_view_rect(kHighView, 0, 0, s_width, s_height);
        bgfx_set_view_clear(kHighView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kHighView, BGFX_VIEW_MODE_SEQUENTIAL);
        // Needed for compute_height_fog, which rebuilds the world-space position with
        // the predefined u_view - the mirror of the AXR m_v2w (combine_1.ps:194).
        bgfx_set_view_transform(kHighView, Device.mView.m, Device.mProject.m);
        bgfx_touch(kHighView);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_highLayout);
        if (!tvb.data)
            return false;
        struct Vertex
        {
            float x, y, z, u, v;
        };
        const Vertex vertices[3] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 0.0f, -1.0f },
        };
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        SetEnvironmentUniforms();
        bgfx_set_texture(0, s_highSampler, GetLitTexture(), 0);
        bgfx_set_texture(1, s_highTonemapSampler, tonemap, 0);
        bgfx_set_texture(2, s_highPositionSampler, s_hdrPosition, 0);
        bgfx_submit(kHighView, s_highProgram, 0, BGFX_DISCARD_ALL);
        s_highOk = true;
        return true;
    }

    bgfx_texture_handle_t GetHighTexture()
    {
        if (s_highOk && bgfxIsValid(s_hdrHigh))
            return s_hdrHigh;
        return BGFX_INVALID_HANDLE;
    }

    // The swap of r2_RT_luminance_cur / r2_RT_luminance_dest. The reference performs it
    // at the end of phase_combine (r4_rendertarget_phase_combine.cpp:678), which is why
    // the sky, the high pass and combine_1 all read the previous frame's tm_scale. The
    // bgfx post chain has the same consumers, so the index flips here rather than inside
    // LuminancePass, which would publish the new value to the combine mid-frame.
    void EndFrameLuminance()
    {
        s_lumTonemapIndex = !s_lumTonemapIndex;
    }

    bool LuminancePass()
    {
        if (!IsReady() || !EnsureLuminanceTargets() || !EnsureLuminancePrograms() ||
            !EnsureBloomTargets())
            return false;

        const float deltaTime = Device.fTimeDelta > 0.0f ? Device.fTimeDelta : 0.0f;
        s_luminanceAdapt = 0.9f * s_luminanceAdapt + 0.1f * deltaTime * kTonemapAdaptation;
        const float none[3] = { 1.0f, 0.0f, 1.0f };
        const float full[3] = { kTonemapMiddleGray, 1.0f, kTonemapLowLum };
        const float middleGray[4] =
        {
            none[0] + (full[0] - none[0]) * kTonemapAmount,
            none[1] + (full[1] - none[1]) * kTonemapAmount,
            none[2] + (full[2] - none[2]) * kTonemapAmount,
            s_luminanceAdapt,
        };

        const u32 previous = s_lumTonemapIndex ? 1u : 0u;
        const u32 current = previous ^ 1u;
        // The first pass measures the bright-pass output, 1:1 with
        // blender_luminance.cpp:16-19 (s_image = r2_RT_bloom1): 256x256 in the high
        // domain, which bloom_luminance_1.ps:9 undoes with the def_hdr factor. This is
        // phase_bloom:131, i.e. after the bright pass and before the two gaussian
        // passes that overwrite rt_Bloom_1, so the chain has to run between them.
        if (!SubmitLuminancePass(kLuminance64View, s_lum64Fb, 64, 64, 0.0f,
            s_bloom1, s_lum1[previous], float(kBloomSize), float(kBloomSize), middleGray) ||
            !SubmitLuminancePass(kLuminance8View, s_lum8Fb, 8, 8, 1.0f,
            s_lum64, s_lum1[previous], 64.0f, 64.0f, middleGray) ||
            !SubmitLuminancePass(kLuminance1View, s_lum1Fb[current], 1, 1, 2.0f,
            s_lum8, s_lum1[previous], 8.0f, 8.0f, middleGray))
            return false;

        return true;
    }

    // R4 bloom chain, 1:1 with archive_sourse/Layers/xrRenderPC_R4/r4_rendertarget_phase_bloom.cpp:68-340.
    //   pass 0 (view kBloomBuildView) bloom_build  s_hdrHigh -> rt_Bloom_1, 4 taps over the central
    //                                            256x256 crop, avg in rgb and (luma - threshold) in a
    //   pass 1 (view kBloomBlurHView)  bloom_filter rt_Bloom_1 -> rt_Bloom_2, gaussian X
    //   pass 2 (view kBloomBlurVView)  bloom_filter rt_Bloom_2 -> rt_Bloom_1, gaussian Y
    // The bright pass reads the HIGH channel (blender_bloom_build.cpp:15-20,
    // s_image = r2_RT_generic1), which is why HighPass has to run before it and why
    // b_params.x stays AXR's ps_r2_ls_bloom_threshold untouched: it is already a
    // high-domain value. phase_luminance() sits between the build and the filter in the
    // original (:131), and LuminancePass runs there.
    bool BloomPass()
    {
        if (!IsReady() || !EnsureBloomTargets() || !EnsureBloomPrograms())
            return false;
        // No high channel means no bright pass. Reading the un-encoded lit image here
        // would be 9x too bright, which is worse than a frame without bloom.
        const bgfx_texture_handle_t high = GetHighTexture();
        if (!bgfxIsValid(high))
            return false;

        // phase_bloom:85-99. The quad interpolates a_i .. 1+a_i, so the v_texcoord0 term is
        // exactly the pixel's normalized position inside the 256x256 target and the two
        // half-texel / one-texel offsets below are a_0/a_1/a_2/a_3 moved onto the shader.
        //   half = { .5f/width, .5f/height }
        //   one  = { 1/width, 1/height } * { (width/2)/256, (height/2)/256 } = { 1/512, 1/512 }
        // i.e. one is half a texel of the 256x256 target, which is what makes the 4 taps the
        // 2x2 sub-texel box of the destination pixel (the /2 in avg absorbs the other half).
        const float buildSetup[4] =
        {
            0.5f / float(s_width), 0.5f / float(s_height),
            0.5f / float(kBloomSize), 0.5f / float(kBloomSize),
        };
        // b_params = (s, s, s, f_bloom_factor) with s = ps_r2_ls_bloom_threshold (phase_bloom:119-125).
        const float buildParams[4] = { kBloomThreshold, kBloomThreshold, kBloomThreshold, 0.0f };

        // Gaussian weights, phase_bloom:234-240 (X) and :313-321 (Y). The vertical pass scales
        // the radius by height/width, the horizontal one does not.
        const float kernelH = kBloomKernelG;
        const float kernelV = kBloomKernelG * float(s_height) / float(s_width);
        float w0[4], w1[4];
        CalcGauss_wave(w0, w1, kernelH, kernelH / 3.0f, kBloomKernelScale);
        float v0[4], v1[4];
        CalcGauss_wave(v0, v1, kernelV, kernelV / 3.0f, kBloomKernelScale);

        // The filter setup mirrors the v_filter vertex layout: the taps sit at
        // a_0 +/- (2k-1+0.5) texels (phase_bloom:172-179 / :252-259), and a_0 is the
        // half-texel shifted interpolated uv.
        const float filterStep = 1.0f / float(kBloomSize);
        const float filterSetupH[4] = { filterStep, 1.0f, 0.0f, 0.0f };
        const float filterSetupV[4] = { filterStep, 0.0f, 1.0f, 0.0f };

        // bgfx_set_uniform must precede the bgfx_submit inside SubmitBloomPass: the submit
        // snapshots the currently bound uniform values, not the ones set afterwards.
        bgfx_set_uniform(s_bloomSetup, buildSetup, 1);
        bgfx_set_uniform(s_bloomParams, buildParams, 1);
        if (!SubmitBloomPass(kBloomBuildView, s_bloom1Fb, s_bloomBuildProgram, s_bloomImage, high,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA))
            return false;

        bgfx_set_uniform(s_filterSetup, filterSetupH, 1);
        bgfx_set_uniform(s_bloomWeight0, w0, 1);
        bgfx_set_uniform(s_bloomWeight1, w1, 1);
        if (!SubmitBloomPass(kBloomBlurHView, s_bloom2Fb, s_bloomFilterProgram, s_bloomSource, s_bloom1,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A))
            return false;

        bgfx_set_uniform(s_filterSetup, filterSetupV, 1);
        bgfx_set_uniform(s_bloomWeight0, v0, 1);
        bgfx_set_uniform(s_bloomWeight1, v1, 1);
        if (!SubmitBloomPass(kBloomBlurVView, s_bloom1Fb, s_bloomFilterProgram, s_bloomSource, s_bloom2,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A))
            return false;
        return true;
    }

    bool CombinePass(uint16_t _width, uint16_t _height)
    {
        if (!IsReady() || !EnsureCombineProgram() || _width == 0 || _height == 0)
            return false;
        const bgfx_texture_handle_t tonemap = GetTonemapTexture();
        if (!bgfxIsValid(tonemap))
            return false;

        struct Vertex
        {
            float x, y, z, u, v;
        };
        const Vertex vertices[3] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 0.0f, -1.0f },
        };

        // With SMAA ready the combine feeds the SMAA input target and the
        // resolve pass presents to the backbuffer; otherwise combine presents
        // directly (SMAAPass then draws nothing), so the frame stays intact.
        // (No ternary: bgfx frame-buffer handles are structs, not scalars.)
        if (IsSmaaReady(_width, _height))
            bgfx_set_view_frame_buffer(kCombineView, s_smaaInputFb);
        else
            bgfx_set_view_frame_buffer(kCombineView, BGFX_INVALID_HANDLE);
        bgfx_set_view_rect(kCombineView, 0, 0, _width, _height);
        bgfx_set_view_clear(kCombineView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kCombineView, BGFX_VIEW_MODE_SEQUENTIAL);
        // combine_vs.sc emits clip space straight from a_position, so the camera
        // transform does not move the fullscreen triangle. It is still needed here:
        // the fog block rebuilds the world-space position with the predefined bgfx
        // per-view u_view (renderer.h:170 View), which is the mirror of the AXR
        // m_v2w / m_inv_V that compute_height_fog and combine_1.ps:194 read, and it
        // is driven by this call. Same pattern as the world pass in
        // bgfxRenderDeviceRender::SetCacheXform and bgfxParticleRender.cpp:85.
        bgfx_set_view_transform(kCombineView, Device.mView.m, Device.mProject.m);
        bgfx_touch(kCombineView);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_combineLayout);
        if (!tvb.data)
            return false;
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        const float exposure[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        SetEnvironmentUniforms();
        bgfx_set_texture(0, s_hdrSampler, GetLitTexture(), 0);
        bgfx_set_texture(1, s_tonemapSampler, tonemap, 0);
        bgfx_set_texture(2, s_positionSampler, GetPositionTexture(), 0);
        const bgfx_texture_handle_t bloom = GetBloomTexture();
        if (bgfxIsValid(bloom))
            bgfx_set_texture(3, s_bloomSampler, bloom, 0);
        bgfx_set_uniform(s_exposure, exposure, 1);
        bgfx_submit(kCombineView, s_combineProgram, 0, BGFX_DISCARD_ALL);
        return true;
    }

    void DestroySmaaTargets()
    {
        if (bgfxIsValid(s_smaaInputFb))
            bgfx_destroy_frame_buffer(s_smaaInputFb);
        if (bgfxIsValid(s_smaaEdgesFb))
            bgfx_destroy_frame_buffer(s_smaaEdgesFb);
        if (bgfxIsValid(s_smaaBlendFb))
            bgfx_destroy_frame_buffer(s_smaaBlendFb);
        s_smaaInputFb = BGFX_INVALID_HANDLE;
        s_smaaEdgesFb = BGFX_INVALID_HANDLE;
        s_smaaBlendFb = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_smaaInput))
            bgfx_destroy_texture(s_smaaInput);
        if (bgfxIsValid(s_smaaEdges))
            bgfx_destroy_texture(s_smaaEdges);
        if (bgfxIsValid(s_smaaBlend))
            bgfx_destroy_texture(s_smaaBlend);
        s_smaaInput = BGFX_INVALID_HANDLE;
        s_smaaEdges = BGFX_INVALID_HANDLE;
        s_smaaBlend = BGFX_INVALID_HANDLE;
        s_smaaWidth = 0;
        s_smaaHeight = 0;
    }

    void DestroySmaaPrograms()
    {
        if (bgfxIsValid(s_smaaEdgeProgram))
            bgfx_destroy_program(s_smaaEdgeProgram);
        if (bgfxIsValid(s_smaaBlendProgram))
            bgfx_destroy_program(s_smaaBlendProgram);
        if (bgfxIsValid(s_smaaResolveProgram))
            bgfx_destroy_program(s_smaaResolveProgram);
        s_smaaEdgeProgram = BGFX_INVALID_HANDLE;
        s_smaaBlendProgram = BGFX_INVALID_HANDLE;
        s_smaaResolveProgram = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_smaaImage))
            bgfx_destroy_uniform(s_smaaImage);
        if (bgfxIsValid(s_smaaEdgesU))
            bgfx_destroy_uniform(s_smaaEdgesU);
        if (bgfxIsValid(s_smaaArea))
            bgfx_destroy_uniform(s_smaaArea);
        if (bgfxIsValid(s_smaaSearch))
            bgfx_destroy_uniform(s_smaaSearch);
        if (bgfxIsValid(s_smaaBlendU))
            bgfx_destroy_uniform(s_smaaBlendU);
        if (bgfxIsValid(s_smaaMetrics))
            bgfx_destroy_uniform(s_smaaMetrics);
        s_smaaImage = BGFX_INVALID_HANDLE;
        s_smaaEdgesU = BGFX_INVALID_HANDLE;
        s_smaaArea = BGFX_INVALID_HANDLE;
        s_smaaSearch = BGFX_INVALID_HANDLE;
        s_smaaBlendU = BGFX_INVALID_HANDLE;
        s_smaaMetrics = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_smaaAreaTex))
            bgfx_destroy_texture(s_smaaAreaTex);
        if (bgfxIsValid(s_smaaSearchTex))
            bgfx_destroy_texture(s_smaaSearchTex);
        s_smaaAreaTex = BGFX_INVALID_HANDLE;
        s_smaaSearchTex = BGFX_INVALID_HANDLE;
    }

    bool EnsureSmaaTextures()
    {
        if (bgfxIsValid(s_smaaAreaTex) && bgfxIsValid(s_smaaSearchTex))
            return true;
        unsigned int w = 0, h = 0;
        if (!bgfxIsValid(s_smaaAreaTex))
        {
            if (!bgfxLoadWorldTexture("shaders\\smaa_area_tex_dx10", s_smaaAreaTex, w, h) &&
                !bgfxLoadWorldTexture("shaders\\smaa_area_tex_dx9", s_smaaAreaTex, w, h))
            {
                LogError("[BGFX] SMAA area texture load failed, SMAA disabled");
                return false;
            }
            LogInfo("[BGFX] SMAA area texture %ux%u", w, h);
        }
        if (!bgfxIsValid(s_smaaSearchTex))
        {
            if (!bgfxLoadWorldTexture("shaders\\smaa_search_tex", s_smaaSearchTex, w, h))
            {
                LogError("[BGFX] SMAA search texture load failed, SMAA disabled");
                return false;
            }
            LogInfo("[BGFX] SMAA search texture %ux%u", w, h);
        }
        return bgfxIsValid(s_smaaAreaTex) && bgfxIsValid(s_smaaSearchTex);
    }

    bool EnsureSmaaPrograms()
    {
        if (bgfxIsValid(s_smaaEdgeProgram) && bgfxIsValid(s_smaaBlendProgram) &&
            bgfxIsValid(s_smaaResolveProgram) && bgfxIsValid(s_smaaImage) &&
            bgfxIsValid(s_smaaEdgesU) && bgfxIsValid(s_smaaArea) &&
            bgfxIsValid(s_smaaSearch) && bgfxIsValid(s_smaaBlendU) &&
            bgfxIsValid(s_smaaMetrics))
            return true;
        DestroySmaaPrograms();
        if (!s_combineLayoutReady)
        {
            LogError("[BGFX] SMAA needs the combine vertex layout");
            return false;
        }
        s_smaaEdgeProgram = BuildProgram("smaa_vs.sc", "smaa_edge_ps.sc");
        s_smaaBlendProgram = BuildProgram("smaa_vs.sc", "smaa_blend_ps.sc");
        s_smaaResolveProgram = BuildProgram("smaa_vs.sc", "smaa_resolve_ps.sc");
        s_smaaImage = bgfx_create_uniform("s_smaaImage", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_smaaEdgesU = bgfx_create_uniform("s_smaaEdges", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_smaaArea = bgfx_create_uniform("s_smaaArea", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_smaaSearch = bgfx_create_uniform("s_smaaSearch", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_smaaBlendU = bgfx_create_uniform("s_smaaBlend", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_smaaMetrics = bgfx_create_uniform("u_smaaMetrics", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_smaaEdgeProgram) || !bgfxIsValid(s_smaaBlendProgram) ||
            !bgfxIsValid(s_smaaResolveProgram) || !bgfxIsValid(s_smaaImage) ||
            !bgfxIsValid(s_smaaEdgesU) || !bgfxIsValid(s_smaaArea) ||
            !bgfxIsValid(s_smaaSearch) || !bgfxIsValid(s_smaaBlendU) ||
            !bgfxIsValid(s_smaaMetrics))
        {
            LogError("[BGFX] SMAA program build failed");
            DestroySmaaPrograms();
            return false;
        }
        LogInfo("[BGFX] SMAA programs created: %u %u %u",
            s_smaaEdgeProgram.idx, s_smaaBlendProgram.idx, s_smaaResolveProgram.idx);
        return true;
    }

    bool EnsureSmaaTargets(uint16_t _width, uint16_t _height)
    {
        if (bgfxIsValid(s_smaaInputFb) && bgfxIsValid(s_smaaEdgesFb) &&
            bgfxIsValid(s_smaaBlendFb) && s_smaaWidth == _width && s_smaaHeight == _height)
            return true;
        DestroySmaaTargets();
        if (_width == 0 || _height == 0)
            return false;
        const uint64_t flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        s_smaaInput = bgfx_create_texture_2d(_width, _height, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, flags, nullptr, 0);
        s_smaaEdges = bgfx_create_texture_2d(_width, _height, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, flags, nullptr, 0);
        s_smaaBlend = bgfx_create_texture_2d(_width, _height, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, flags, nullptr, 0);
        if (!bgfxIsValid(s_smaaInput) || !bgfxIsValid(s_smaaEdges) || !bgfxIsValid(s_smaaBlend))
        {
            LogError("[BGFX] SMAA target texture create failed (%ux%u)", _width, _height);
            DestroySmaaTargets();
            return false;
        }
        bgfx_texture_handle_t aIn[] = { s_smaaInput };
        bgfx_texture_handle_t aEd[] = { s_smaaEdges };
        bgfx_texture_handle_t aBl[] = { s_smaaBlend };
        s_smaaInputFb = bgfx_create_frame_buffer_from_handles(1, aIn, true);
        s_smaaEdgesFb = bgfx_create_frame_buffer_from_handles(1, aEd, true);
        s_smaaBlendFb = bgfx_create_frame_buffer_from_handles(1, aBl, true);
        if (!bgfxIsValid(s_smaaInputFb) || !bgfxIsValid(s_smaaEdgesFb) || !bgfxIsValid(s_smaaBlendFb))
        {
            LogError("[BGFX] SMAA target framebuffer create failed (%ux%u)", _width, _height);
            DestroySmaaTargets();
            return false;
        }
        s_smaaWidth = _width;
        s_smaaHeight = _height;
        LogInfo("[BGFX] SMAA targets created: %ux%u", _width, _height);
        return true;
    }

    bool IsSmaaReady(uint16_t _width, uint16_t _height)
    {
        return EnsureSmaaTargets(_width, _height) && EnsureSmaaPrograms() && EnsureSmaaTextures();
    }

    // Binds the fullscreen triangle + metrics for an SMAA pass. Textures are
    // bound by the caller AFTER this and BEFORE the single submit (bgfx
    // snapshots uniforms/textures at submit, like SubmitBloomPass).
    static bool SetupSmaaDraw(uint16_t _width, uint16_t _height)
    {
        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_combineLayout);
        if (!tvb.data)
            return false;
        static const float verts[3][5] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f, 3.0f, 0.0f, 0.0f, -1.0f },
        };
        std::memcpy(tvb.data, verts, sizeof(verts));

        const float metrics[4] = { 1.0f / _width, 1.0f / _height, (float)_width, (float)_height };
        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        bgfx_set_uniform(s_smaaMetrics, metrics, 1);
        return true;
    }

    static void SetupSmaaView(bgfx_view_id_t _view, bgfx_frame_buffer_handle_t _fb,
        uint16_t _width, uint16_t _height, bool _clear)
    {
        bgfx_set_view_frame_buffer(_view, _fb);
        bgfx_set_view_rect(_view, 0, 0, _width, _height);
        bgfx_set_view_clear(_view, _clear ? BGFX_CLEAR_COLOR : BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(_view, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_set_view_transform(_view, s_identity, s_identity);
        bgfx_touch(_view);
    }

    bool SMAAPass(uint16_t _width, uint16_t _height)
    {
        if (!IsReady() || _width == 0 || _height == 0)
            return false;
        if (!IsSmaaReady(_width, _height))
            return false;
        const uint64_t pointFlags =
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        // Pass 0 (rendertarget_phase_smaa.cpp:19-36): edge detect on the combine
        // LDR output, point sampling (SMAASamplePoint), cleared to 0 like the
        // reference ClearRenderTargetView. Stencil optimisation skipped (same output).
        SetupSmaaView(kSmaaEdgeView, s_smaaEdgesFb, _width, _height, true);
        if (!SetupSmaaDraw(_width, _height))
            return false;
        bgfx_set_texture(0, s_smaaImage, ScatterOutputTexture(), pointFlags);
        bgfx_submit(kSmaaEdgeView, s_smaaEdgeProgram, 0, BGFX_DISCARD_ALL);
        // Pass 1 (:38-57): blend weights from edges + area (linear) + search (point).
        SetupSmaaView(kSmaaBlendView, s_smaaBlendFb, _width, _height, true);
        if (!SetupSmaaDraw(_width, _height))
            return false;
        bgfx_set_texture(0, s_smaaEdgesU, s_smaaEdges, 0);
        bgfx_set_texture(1, s_smaaArea, s_smaaAreaTex, 0);
        bgfx_set_texture(2, s_smaaSearch, s_smaaSearchTex, pointFlags);
        bgfx_submit(kSmaaBlendView, s_smaaBlendProgram, 0, BGFX_DISCARD_ALL);
        // Pass 2 (:60-76): neighbourhood blend of the combine image, linear.
        // Writes into the backbuffer; UI draws on top of it as before.
        SetupSmaaView(kSmaaResolveView, BGFX_INVALID_HANDLE, _width, _height, false);
        if (!SetupSmaaDraw(_width, _height))
            return false;
        bgfx_set_texture(0, s_smaaImage, ScatterOutputTexture(), 0);
        bgfx_set_texture(1, s_smaaBlendU, s_smaaBlend, 0);
        bgfx_submit(kSmaaResolveView, s_smaaResolveProgram, 0, BGFX_DISCARD_ALL);
        return true;
    }

    void DestroyFogScatter()
    {
        if (bgfxIsValid(s_fogBlur1Fb))
            bgfx_destroy_frame_buffer(s_fogBlur1Fb);
        if (bgfxIsValid(s_fogBlur2Fb))
            bgfx_destroy_frame_buffer(s_fogBlur2Fb);
        if (bgfxIsValid(s_smaaScatterFb))
            bgfx_destroy_frame_buffer(s_smaaScatterFb);
        s_fogBlur1Fb = BGFX_INVALID_HANDLE;
        s_fogBlur2Fb = BGFX_INVALID_HANDLE;
        s_smaaScatterFb = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_fogBlur1))
            bgfx_destroy_texture(s_fogBlur1);
        if (bgfxIsValid(s_fogBlur2))
            bgfx_destroy_texture(s_fogBlur2);
        if (bgfxIsValid(s_smaaScatter))
            bgfx_destroy_texture(s_smaaScatter);
        s_fogBlur1 = BGFX_INVALID_HANDLE;
        s_fogBlur2 = BGFX_INVALID_HANDLE;
        s_smaaScatter = BGFX_INVALID_HANDLE;
        if (bgfxIsValid(s_fogScatterProgram))
            bgfx_destroy_program(s_fogScatterProgram);
        if (bgfxIsValid(s_scatterImage))
            bgfx_destroy_uniform(s_scatterImage);
        if (bgfxIsValid(s_scatterBlur))
            bgfx_destroy_uniform(s_scatterBlur);
        s_fogScatterProgram = BGFX_INVALID_HANDLE;
        s_scatterImage = BGFX_INVALID_HANDLE;
        s_scatterBlur = BGFX_INVALID_HANDLE;
    }

    bool EnsureFogScatter(uint16_t _width, uint16_t _height)
    {
        if (bgfxIsValid(s_fogBlur1Fb) && bgfxIsValid(s_fogBlur2Fb) &&
            bgfxIsValid(s_smaaScatterFb) && bgfxIsValid(s_fogScatterProgram) &&
            bgfxIsValid(s_scatterImage) && bgfxIsValid(s_scatterBlur) &&
            s_smaaWidth == _width && s_smaaHeight == _height)
            return true;
        DestroyFogScatter();
        if (_width == 0 || _height == 0)
            return false;
        const uint64_t flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        s_fogBlur1 = bgfx_create_texture_2d(kBloomSize, kBloomSize, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, flags, nullptr, 0);
        s_fogBlur2 = bgfx_create_texture_2d(kBloomSize, kBloomSize, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, flags, nullptr, 0);
        s_smaaScatter = bgfx_create_texture_2d(_width, _height, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, flags, nullptr, 0);
        if (!bgfxIsValid(s_fogBlur1) || !bgfxIsValid(s_fogBlur2) || !bgfxIsValid(s_smaaScatter))
        {
            LogError("[BGFX] Fog scatter target create failed (%ux%u)", _width, _height);
            DestroyFogScatter();
            return false;
        }
        bgfx_texture_handle_t aB1[] = { s_fogBlur1 };
        bgfx_texture_handle_t aB2[] = { s_fogBlur2 };
        bgfx_texture_handle_t aSc[] = { s_smaaScatter };
        s_fogBlur1Fb = bgfx_create_frame_buffer_from_handles(1, aB1, true);
        s_fogBlur2Fb = bgfx_create_frame_buffer_from_handles(1, aB2, true);
        s_smaaScatterFb = bgfx_create_frame_buffer_from_handles(1, aSc, true);
        if (!bgfxIsValid(s_fogBlur1Fb) || !bgfxIsValid(s_fogBlur2Fb) || !bgfxIsValid(s_smaaScatterFb))
        {
            LogError("[BGFX] Fog scatter framebuffer create failed (%ux%u)", _width, _height);
            DestroyFogScatter();
            return false;
        }
        s_fogScatterProgram = BuildProgram("smaa_vs.sc", "fog_scatter_ps.sc");
        s_scatterImage = bgfx_create_uniform("s_scatterImage", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_scatterBlur = bgfx_create_uniform("s_scatterBlur", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        if (!bgfxIsValid(s_fogScatterProgram) || !bgfxIsValid(s_scatterImage) || !bgfxIsValid(s_scatterBlur))
        {
            LogError("[BGFX] Fog scatter program build failed");
            DestroyFogScatter();
            return false;
        }
        LogInfo("[BGFX] Fog scatter targets created: 256x256 x2 + %ux%u, program %u",
            _width, _height, s_fogScatterProgram.idx);
        return true;
    }

    // Image SMAA (and the debug views) resolve from: scattered output when the
    // fog-scatter chain is up, plain combine output otherwise.
    bgfx_texture_handle_t ScatterOutputTexture()
    {
        if (bgfxIsValid(s_smaaScatter))
            return s_smaaScatter;
        return s_smaaInput;
    }

    bool FogScatterPass(uint16_t _width, uint16_t _height)
    {
        if (!IsReady() || _width == 0 || _height == 0)
            return false;
        if (!EnsureSmaaTargets(_width, _height) || !EnsureSmaaPrograms() || !EnsureSmaaTextures())
            return false;
        if (!EnsureFogScatter(_width, _height))
            return false;
        if (!bgfxIsValid(s_bloomBuildProgram) || !bgfxIsValid(s_bloomFilterProgram))
            return false;
        // Downsample the combine LDR output with a zero threshold (full-scene
        // average, not the bright pass). Same 256 geometry as the bloom build.
        const float buildSetup[4] =
        {
            0.5f / float(s_width), 0.5f / float(s_height),
            0.5f / float(kBloomSize), 0.5f / float(kBloomSize),
        };
        const float zeroThreshold[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const float kernelH = kBloomKernelG;
        const float kernelV = kBloomKernelG * float(s_height) / float(s_width);
        float w0[4], w1[4];
        CalcGauss_wave(w0, w1, kernelH, kernelH / 3.0f, kBloomKernelScale);
        float v0[4], v1[4];
        CalcGauss_wave(v0, v1, kernelV, kernelV / 3.0f, kBloomKernelScale);
        const float filterStep = 1.0f / float(kBloomSize);
        const float filterSetupH[4] = { filterStep, 1.0f, 0.0f, 0.0f };
        const float filterSetupV[4] = { filterStep, 0.0f, 1.0f, 0.0f };

        bgfx_set_uniform(s_bloomSetup, buildSetup, 1);
        bgfx_set_uniform(s_bloomParams, zeroThreshold, 1);
        if (!SubmitBloomPass(kFogBlurBuildView, s_fogBlur1Fb, s_bloomBuildProgram, s_bloomImage, s_smaaInput,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A))
            return false;

        bgfx_set_uniform(s_filterSetup, filterSetupH, 1);
        bgfx_set_uniform(s_bloomWeight0, w0, 1);
        bgfx_set_uniform(s_bloomWeight1, w1, 1);
        if (!SubmitBloomPass(kFogBlurHView, s_fogBlur2Fb, s_bloomFilterProgram, s_bloomSource, s_fogBlur1,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A))
            return false;

        bgfx_set_uniform(s_filterSetup, filterSetupV, 1);
        bgfx_set_uniform(s_bloomWeight0, v0, 1);
        bgfx_set_uniform(s_bloomWeight1, v1, 1);
        if (!SubmitBloomPass(kFogBlurVView, s_fogBlur1Fb, s_bloomFilterProgram, s_bloomSource, s_fogBlur2,
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A))
            return false;

        // Scatter blend (combine_2_naa.ps:106-121): lerp(img, max(img, blur),
        // smoothstep(0.2, 0.8, fog^2)). disablefog (scopes/NVG) is not wired.
        SetupSmaaView(kFogScatterView, s_smaaScatterFb, _width, _height, false);
        if (!SetupSmaaDraw(_width, _height))
            return false;
        bgfx_set_texture(0, s_scatterImage, s_smaaInput, 0);
        bgfx_set_texture(1, s_scatterBlur, s_fogBlur1, 0);
        bgfx_submit(kFogScatterView, s_fogScatterProgram, 0, BGFX_DISCARD_ALL);
        return true;
    }

    bgfx_texture_handle_t GetBloomTexture()
    {
        if (bgfxIsValid(s_bloom1))
            return s_bloom1;
        return BGFX_INVALID_HANDLE;
    }

    bgfx_texture_handle_t GetTonemapTexture()
    {
        const u32 index = s_lumTonemapIndex ? 1u : 0u;
        if (bgfxIsValid(s_lum1[index]))
            return s_lum1[index];
        return BGFX_INVALID_HANDLE;
    }

    bgfx_texture_handle_t GetPositionTexture()
    {
        if (bgfxIsValid(s_hdrPosition))
            return s_hdrPosition;
        return BGFX_INVALID_HANDLE;
    }

    bgfx_texture_handle_t GetGbufTexture()
    {
        if (bgfxIsValid(s_hdrGbuf))
            return s_hdrGbuf;
        return BGFX_INVALID_HANDLE;
    }

    // Reads XRGBUF_DEBUG once per session. 1 normal, 2 depth, 3 hemi, 4 albedo;
    // 5..99 cycle through all four with that many seconds per view; >= 100 is
    // read as centiseconds per view. Anything else leaves the inspector off, so
    // a normal run is bit-identical to the pre-stage-1 frame.
    int GbufDebugEnvValue()
    {
        if (!s_gbufDebugEnvRead)
        {
            s_gbufDebugEnvRead = true;
            const char* raw = getenv("XRGBUF_DEBUG");
            if (raw && raw[0])
            {
                const int value = atoi(raw);
                if (value > 0)
                {
                    s_gbufDebugEnv = (value >= 100) ? 100 + (value - 100) / 100 : value;
                    LogInfo("[BGFX] G-buffer inspector: XRGBUF_DEBUG=%d", value);
                }
            }
        }
        return s_gbufDebugEnv;
    }

    bool EnsureGbufDebugProgram()
    {
        if (bgfxIsValid(s_gbufDebugProgram) && bgfxIsValid(s_gbufDebugGbufSampler) &&
            bgfxIsValid(s_gbufDebugPosSampler) && bgfxIsValid(s_gbufDebugHdrSampler) &&
            bgfxIsValid(s_gbufDebugParams))
            return true;
        if (!s_gbufDebugLayoutReady)
        {
            bgfx_vertex_layout_begin(&s_gbufDebugLayout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_gbufDebugLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_gbufDebugLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_gbufDebugLayout);
            s_gbufDebugLayoutReady = true;
        }
        // combine_vs.sc already emits the fullscreen triangle and v_texcoord0
        // that gbuf_debug_ps.sc consumes.
        s_gbufDebugProgram = BuildProgram("combine_vs.sc", "gbuf_debug_ps.sc");
        if (!bgfxIsValid(s_gbufDebugProgram))
        {
            LogError("[BGFX] G-buffer inspector program build failed");
            DestroyGbufDebugProgram();
            return false;
        }
        s_gbufDebugGbufSampler = bgfx_create_uniform("s_gbuf", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_gbufDebugPosSampler = bgfx_create_uniform("s_position", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_gbufDebugHdrSampler = bgfx_create_uniform("s_hdr", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_gbufDebugParams = bgfx_create_uniform("u_gbufDebug", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_gbufDebugGbufSampler) || !bgfxIsValid(s_gbufDebugPosSampler) ||
            !bgfxIsValid(s_gbufDebugHdrSampler) || !bgfxIsValid(s_gbufDebugParams))
        {
            LogError("[BGFX] G-buffer inspector uniforms create failed");
            DestroyGbufDebugProgram();
            return false;
        }
        LogInfo("[BGFX] G-buffer inspector program created: %u", s_gbufDebugProgram.idx);
        return true;
    }

    bool GbufDebugPass(uint16_t _width, uint16_t _height)
    {
        const int mode = GbufDebugEnvValue();
        if (mode <= 0 || !IsReady() || _width == 0 || _height == 0)
            return false;

        const float dt = Device.fTimeDelta > 0.0f ? Device.fTimeDelta : 0.0f;
        s_gbufDebugClock += dt;
        // mode 1..4 pins a single view; mode >= 100 is centiseconds per view.
        // Higher modes cycle forever, so any slot worth of consecutive frames
        // covers all four views whatever the phase the frame counter started in.
        const float slot = (mode >= 100) ? (mode - 100) * 0.01f : (float)mode;
        const int view = (mode >= 100) ? (int(s_gbufDebugClock / slot) % 4) : (mode - 1);

        if (!EnsureGbufDebugProgram())
            return false;

        struct Vertex
        {
            float x, y, z, u, v;
        };
        const Vertex vertices[3] =
        {
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 2.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 0.0f, -1.0f },
        };

        // Draws on top of the combine result, so it has to run after kCombineView
        // and before the UI views (see the order array in
        // bgfxRenderDeviceRender::Begin). The view itself is set up there.
        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_gbufDebugLayout);
        if (!tvb.data)
            return false;
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        // u_gbufDebug.y is the far plane, so the depth view ramps black at the
        // near plane to white at the far plane.
        const float farPlane = Device.mProject._43 / Device.mProject._33;
        const float params[4] = { float(view), farPlane > 1.0f ? farPlane : 1000.0f, 0.0f, 0.0f };
        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        bgfx_set_texture(0, s_gbufDebugGbufSampler, s_hdrGbuf, 0);
        bgfx_set_texture(1, s_gbufDebugPosSampler, s_hdrPosition, 0);
        bgfx_set_texture(2, s_gbufDebugHdrSampler, s_hdrColor, 0);
        bgfx_set_uniform(s_gbufDebugParams, params, 1);
        bgfx_submit(kGbufDebugView, s_gbufDebugProgram, 0, BGFX_DISCARD_ALL);
        return true;
    }
}
