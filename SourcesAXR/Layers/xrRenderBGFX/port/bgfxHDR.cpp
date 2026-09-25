#include "stdafx.h"
#pragma hdrstop

#include "bgfxHDR.h"
#include "../bgfxShaderCompiler.h"
#include "../../../xrEngine/device.h"
#include "../../../xrEngine/x_ray.h"
#include "../../../xrEngine/IGame_Persistent.h"
#include "../../../xrEngine/Environment.h"
#include "../../../xrEngine/DiscordRichPresense.h"

#include <cstring>
#include <vector>

namespace
{
    bgfx_frame_buffer_handle_t s_hdrFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrColor = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrDepth = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_combineProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_hdrSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_tonemapSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_hdrDepthSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_exposure = BGFX_INVALID_HANDLE;
    // SSFX_HEIGHT_FOG globals (game_unpacked/shaders/r3/screenspace_fog.h:11).
    // u_invProj / u_invView are NOT here on purpose: bgfx_shader.sh already declares
    // them as predefined per-view uniforms, fed by bgfx_set_view_transform on kCombineView.
    bgfx_uniform_handle_t s_fogParams = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_fogColor = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_lowlandFogParams = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sunDir = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sunColor = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_combineLayout = {};
    bool s_combineLayoutReady = false;
    uint16_t s_width = 0;
    uint16_t s_height = 0;
    bgfx_texture_format_t s_colorFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
    bgfx_texture_format_t s_depthFormat = BGFX_TEXTURE_FORMAT_D24;

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
        if (bgfxIsValid(s_hdrDepth))
            bgfx_destroy_texture(s_hdrDepth);
        s_hdrColor = BGFX_INVALID_HANDLE;
        s_hdrDepth = BGFX_INVALID_HANDLE;
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

    void DestroyCombineProgram()
    {
        if (bgfxIsValid(s_combineProgram))
            bgfx_destroy_program(s_combineProgram);
        if (bgfxIsValid(s_hdrSampler))
            bgfx_destroy_uniform(s_hdrSampler);
        if (bgfxIsValid(s_tonemapSampler))
            bgfx_destroy_uniform(s_tonemapSampler);
        if (bgfxIsValid(s_hdrDepthSampler))
            bgfx_destroy_uniform(s_hdrDepthSampler);
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
        s_hdrDepthSampler = BGFX_INVALID_HANDLE;
        s_exposure = BGFX_INVALID_HANDLE;
        s_fogParams = BGFX_INVALID_HANDLE;
        s_fogColor = BGFX_INVALID_HANDLE;
        s_lowlandFogParams = BGFX_INVALID_HANDLE;
        s_sunDir = BGFX_INVALID_HANDLE;
        s_sunColor = BGFX_INVALID_HANDLE;
        s_combineLayoutReady = false;
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
            bgfxIsValid(s_hdrDepthSampler) && bgfxIsValid(s_exposure) && bgfxIsValid(s_fogParams) &&
            bgfxIsValid(s_fogColor) && bgfxIsValid(s_lowlandFogParams) && bgfxIsValid(s_sunDir) &&
            bgfxIsValid(s_sunColor))
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
        s_hdrDepthSampler = bgfx_create_uniform("s_hdrDepth", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_exposure = bgfx_create_uniform("u_exposure", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_fogParams = bgfx_create_uniform("u_fogParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_fogColor = bgfx_create_uniform("u_fogColor", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_lowlandFogParams = bgfx_create_uniform("u_lowlandFogParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_sunDir = bgfx_create_uniform("u_sunDir", BGFX_UNIFORM_TYPE_VEC4, 1);
        s_sunColor = bgfx_create_uniform("u_sunColor", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_hdrSampler) || !bgfxIsValid(s_tonemapSampler) || !bgfxIsValid(s_hdrDepthSampler) ||
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

    // SSFX_HEIGHT_FOG globals, 1:1 with the Anomaly R_constant_setup binders in
    // SourcesAXR/Layers/xrRender/Blender_Recorder_StandartBinding.cpp:
    //   fog_params         cl_fog_params         :170-181  (-n*r, n, f, r)
    //   fog_color          cl_fog_color          :184-194  (rgb, density)
    //   lowland_fog_params cl_lowland_fog_params :198-208  (height, density, base height, 0)
    //   Ldynamic_dir/color r2_rendertarget_phase_combine.cpp:155-166 -> the combine
    //     pass evaluates screenspace_fog.h against the view-space sun, so
    //     u_sunDir = normalize(Device.mView * CEnvDescriptor::sun_dir) and
    //     u_sunColor = CEnvDescriptor::sun_color. CurrentEnv is a CEnvDescriptorMixer,
    //     which derives from CEnvDescriptor (Environment.h:258), so both live there.
    void SetFogUniforms()
    {
        float params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float fogColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float lowland[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float sunDir[4] = { 0.0f, -1.0f, 0.0f, 0.0f };
        float sunColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

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
    }
}

namespace bgfxHDR
{
    bool CreateHDRTarget(uint16_t _width, uint16_t _height)
    {
        if (_width == 0 || _height == 0)
            return false;

        if (bgfxIsValid(s_hdrFb) && s_width == _width && s_height == _height)
            return EnsureLuminanceTargets() && EnsureCombineProgram();

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

        const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP;
        const uint64_t depthFlags = BGFX_TEXTURE_RT;
        s_hdrColor = bgfx_create_texture_2d(_width, _height, false, 1, s_colorFormat, colorFlags, nullptr, 0);
        s_hdrDepth = bgfx_create_texture_2d(_width, _height, false, 1, s_depthFormat, depthFlags, nullptr, 0);
        if (!bgfxIsValid(s_hdrColor) || !bgfxIsValid(s_hdrDepth))
        {
            LogError("[BGFX] HDR target texture create failed (%ux%u)", _width, _height);
            DestroyTextures();
            return false;
        }

        bgfx_texture_handle_t attachments[2] = { s_hdrColor, s_hdrDepth };
        s_hdrFb = bgfx_create_frame_buffer_from_handles(2, attachments, true);
        if (!bgfxIsValid(s_hdrFb))
        {
            LogError("[BGFX] HDR target framebuffer create failed (%ux%u)", _width, _height);
            s_hdrFb = BGFX_INVALID_HANDLE;
            DestroyTextures();
            return false;
        }

        s_width = _width;
        s_height = _height;
        LogInfo("[BGFX] HDR target created: %ux%u color=%d depth=%d fb=%u",
            _width, _height, (int)s_colorFormat, (int)s_depthFormat, s_hdrFb.idx);

        if (!CreateLuminanceTargets() || !EnsureCombineProgram())
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
        DestroyTextures();
        s_width = 0;
        s_height = 0;
        DestroyCombineProgram();
        DestroyLuminancePrograms();
        DestroyLuminanceTargets();
    }

    bool RecreateOnResize(uint16_t _width, uint16_t _height)
    {
        if (bgfxIsValid(s_hdrFb) && s_width == _width && s_height == _height)
            return EnsureLuminanceTargets() && EnsureCombineProgram();
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

        bgfx_set_view_frame_buffer(kSceneView, s_hdrFb);
        bgfx_set_view_frame_buffer(kSceneFxView, s_hdrFb);
        bgfx_set_view_rect(kSceneView, 0, 0, s_width, s_height);
        bgfx_set_view_clear(kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
        bgfx_set_view_mode(kSceneView, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_touch(kSceneView);
        bgfx_set_view_rect(kSceneFxView, 0, 0, s_width, s_height);
        bgfx_set_view_clear(kSceneFxView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kSceneFxView, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_touch(kSceneFxView);
        return true;
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
        if (!SubmitLuminancePass(kLuminance64View, s_lum64Fb, 64, 64, 0.0f,
            s_hdrColor, s_lum1[previous], float(s_width), float(s_height), middleGray) ||
            !SubmitLuminancePass(kLuminance8View, s_lum8Fb, 8, 8, 1.0f,
            s_lum64, s_lum1[previous], 64.0f, 64.0f, middleGray) ||
            !SubmitLuminancePass(kLuminance1View, s_lum1Fb[current], 1, 1, 2.0f,
            s_lum8, s_lum1[previous], 8.0f, 8.0f, middleGray))
            return false;

        s_lumTonemapIndex = current != 0;
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

        bgfx_set_view_frame_buffer(kCombineView, BGFX_INVALID_HANDLE);
        bgfx_set_view_rect(kCombineView, 0, 0, _width, _height);
        bgfx_set_view_clear(kCombineView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kCombineView, BGFX_VIEW_MODE_SEQUENTIAL);
        // combine_vs.sc emits clip space straight from a_position, so the camera
        // transform does not move the fullscreen triangle. It is still needed here:
        // the fog block rebuilds the view-space position with u_invProj and the world
        // position with u_invView, which are the predefined bgfx per-view uniforms
        // driven by this call (renderer.h:170 InvView / InvProj). Same pattern as the
        // world pass in bgfxRenderDeviceRender::SetCacheXform and bgfxParticleRender.cpp:85.
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
        SetFogUniforms();
        bgfx_set_texture(0, s_hdrSampler, s_hdrColor, 0);
        bgfx_set_texture(1, s_tonemapSampler, tonemap, 0);
        bgfx_set_texture(2, s_hdrDepthSampler, s_hdrDepth, 0);
        bgfx_set_uniform(s_exposure, exposure, 1);
        bgfx_submit(kCombineView, s_combineProgram, 0, BGFX_DISCARD_ALL);
        return true;
    }

    bgfx_texture_handle_t GetTonemapTexture()
    {
        const u32 index = s_lumTonemapIndex ? 1u : 0u;
        if (bgfxIsValid(s_lum1[index]))
            return s_lum1[index];
        return BGFX_INVALID_HANDLE;
    }
}
