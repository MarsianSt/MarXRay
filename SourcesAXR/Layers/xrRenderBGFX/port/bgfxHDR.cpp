#include "stdafx.h"
#pragma hdrstop

#include "bgfxHDR.h"
#include "../bgfxShaderCompiler.h"
#include "../bgfxUIShader.h"
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
        s_hdrColor = BGFX_INVALID_HANDLE;
        s_hdrPosition = BGFX_INVALID_HANDLE;
        s_hdrGbuf = BGFX_INVALID_HANDLE;
        s_hdrDepth = BGFX_INVALID_HANDLE;
        s_hdrLit = BGFX_INVALID_HANDLE;
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
        s_resolveProgram = BGFX_INVALID_HANDLE;
        s_litSampler = BGFX_INVALID_HANDLE;
        s_litPositionSampler = BGFX_INVALID_HANDLE;
        s_litGbufSampler = BGFX_INVALID_HANDLE;
        s_hemiColor = BGFX_INVALID_HANDLE;
        s_ambientColor = BGFX_INVALID_HANDLE;
        s_envCube0 = BGFX_INVALID_HANDLE;
        s_envCube1 = BGFX_INVALID_HANDLE;
        s_cubeValid = BGFX_INVALID_HANDLE;
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
            bgfxIsValid(s_envCube0) && bgfxIsValid(s_envCube1) && bgfxIsValid(s_cubeValid))
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
        s_hemiColor = bgfx_create_uniform("u_hemiColor", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_ambientColor = bgfx_create_uniform("u_ambient", BGFX_UNIFORM_TYPE_VEC4, 1);
        // env_s0 / env_s1, hmodel.h:14-15, and the 0/1 switch that lets
        // deferred_light_ps.sc fall back to the cube-less constant while the
        // level has no env cube bound.
        s_envCube0 = bgfx_create_uniform("s_env0", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_envCube1 = bgfx_create_uniform("s_env1", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_cubeValid = bgfx_create_uniform("u_cubeValid", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_litSampler) || !bgfxIsValid(s_litPositionSampler) ||
            !bgfxIsValid(s_litGbufSampler) || !bgfxIsValid(s_hemiColor) ||
            !bgfxIsValid(s_ambientColor) || !bgfxIsValid(s_envCube0) ||
            !bgfxIsValid(s_envCube1) || !bgfxIsValid(s_cubeValid))
        {
            LogError("[BGFX] Lighting resolve uniforms create failed");
            DestroyResolveProgram();
            return false;
        }

        LogInfo("[BGFX] Lighting resolve program created: %u", s_resolveProgram.idx);
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
    //   u_hemiColor  = CEnvDescriptor::hemi_color, i.e. L_hemi_color, the source
    //                  calc_model_hemi_r1() reads (common_functions.h:99-101); the
    //                  writers' G-buffer hemi is the bare up factor max(0, Nw.y)
    //                  that this colour scales (gbuf_pack.h)
    //   u_ambient    = CEnvDescriptor::ambient, i.e. L_ambient (hmodel.h:129), with
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

            hemiColor[0] = env->hemi_color.x;
            hemiColor[1] = env->hemi_color.y;
            hemiColor[2] = env->hemi_color.z;
            hemiColor[3] = 0.0f;

            ambient[0] = env->ambient.x;
            ambient[1] = env->ambient.y;
            ambient[2] = env->ambient.z;
            // env_color.w, the cube lerp factor (hmodel.h:105). The mixer carries
            // the weather blend weight, the same slot the reference feeds
            // L_ambient.w with (Blender_Recorder_StandartBinding.cpp:389-390).
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
                EnsureCombineProgram() && EnsureResolveProgram();

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
        if (!bgfxIsValid(s_hdrColor) || !bgfxIsValid(s_hdrPosition) || !bgfxIsValid(s_hdrGbuf) ||
            !bgfxIsValid(s_hdrDepth) || !bgfxIsValid(s_hdrLit))
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
        LogInfo("[BGFX] HDR target created: %ux%u color=%d position=%d gbuf=%d depth=%d fb=%u litfb=%u",
            _width, _height, (int)s_colorFormat, (int)s_positionFormat, (int)s_gbufFormat,
            (int)s_depthFormat, s_hdrFb.idx, s_hdrLitFb.idx);

        if (!CreateLuminanceTargets() || !EnsureBloomTargets() || !EnsureBloomPrograms() ||
            !EnsureCombineProgram() || !EnsureResolveProgram())
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
        DestroyTextures();
        s_width = 0;
        s_height = 0;
        s_gbufDebugClock = 0.0f;
        DestroyCombineProgram();
        DestroyResolveProgram();
        DestroyGbufDebugProgram();
        DestroyBloomPrograms();
        DestroyBloomTargets();
        DestroyLuminancePrograms();
        DestroyLuminanceTargets();
        DestroySmaaTargets();
        DestroySmaaPrograms();
        DestroyFogScatter();
    }

    bool RecreateOnResize(uint16_t _width, uint16_t _height)
    {
        if (bgfxIsValid(s_hdrFb) && s_width == _width && s_height == _height)
            return EnsureLuminanceTargets() && EnsureBloomTargets() && EnsureBloomPrograms() &&
                EnsureCombineProgram() && EnsureResolveProgram();
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
    //         which this pass leaves at 1 (no shadow map is bound).
    //   hemi  hmodel() - the ambient half combine_1.ps:166 adds, i.e.
    //         SRGBToLinear(env_d) * albedo with
    //         env_d = lerp(env_s0, env_s1, env_color.w) * env_col * hemi + L_ambient
    //         (hmodel.h:105, :121, :125, :130), the ambient cube included.
    // Both terms read the per-pixel hemi and the packed normal out of attachment
    // 2 and modulate the gamma-space albedo of attachment 0, exactly like the
    // reference does through gbuffer_load_data() (gbuffer_stage.h:115-143).
    // Pixels nothing drew (sky, clouds) keep their attachment-0 radiance: see
    // the P == 0 branch in deferred_light_ps.sc.
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
        bgfx_set_texture(0, s_litSampler, s_hdrColor, 0);
        bgfx_set_texture(1, s_litPositionSampler, s_hdrPosition, 0);
        bgfx_set_texture(2, s_litGbufSampler, s_hdrGbuf, 0);
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

    bool LuminancePass()
    {
        if (!IsReady() || !EnsureLuminanceTargets() || !EnsureLuminancePrograms())
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
        // The middle-grey measurement runs on the lit image: the luminance chain
        // in AXR sees the accumulator after accum_sun/hmodel, not the bare albedo.
        const bgfx_texture_handle_t lit = GetLitTexture();
        if (!SubmitLuminancePass(kLuminance64View, s_lum64Fb, 64, 64, 0.0f,
            lit, s_lum1[previous], float(s_width), float(s_height), middleGray) ||
            !SubmitLuminancePass(kLuminance8View, s_lum8Fb, 8, 8, 1.0f,
            s_lum64, s_lum1[previous], 64.0f, 64.0f, middleGray) ||
            !SubmitLuminancePass(kLuminance1View, s_lum1Fb[current], 1, 1, 2.0f,
            s_lum8, s_lum1[previous], 8.0f, 8.0f, middleGray))
            return false;

        s_lumTonemapIndex = current != 0;
        return true;
    }

    // R4 bloom chain, 1:1 with archive_sourse/Layers/xrRenderPC_R4/r4_rendertarget_phase_bloom.cpp:68-340.
    //   pass 0 (view kBloomBuildView) bloom_build  s_hdr -> rt_Bloom_1, 4 taps over the central
    //                                            256x256 crop, avg in rgb and (luma - threshold) in a
    //   pass 1 (view kBloomBlurHView)  bloom_filter rt_Bloom_1 -> rt_Bloom_2, gaussian X
    //   pass 2 (view kBloomBlurVView)  bloom_filter rt_Bloom_2 -> rt_Bloom_1, gaussian Y
    // phase_luminance() sits between the build and the filter in the original (:131); here the
    // luminance chain already runs earlier in the frame, so the relative order is unchanged.
    bool BloomPass()
    {
        if (!IsReady() || !EnsureBloomTargets() || !EnsureBloomPrograms())
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
        if (!SubmitBloomPass(kBloomBuildView, s_bloom1Fb, s_bloomBuildProgram, s_bloomImage, GetLitTexture(),
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
