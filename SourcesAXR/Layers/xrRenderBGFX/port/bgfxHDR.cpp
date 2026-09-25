#include "stdafx.h"
#pragma hdrstop

#include "bgfxHDR.h"
#include "../bgfxShaderCompiler.h"

#include <cstring>
#include <vector>

namespace
{
    bgfx_frame_buffer_handle_t s_hdrFb = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrColor = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t s_hdrDepth = BGFX_INVALID_HANDLE;
    bgfx_program_handle_t s_combineProgram = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_hdrSampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_exposure = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_combineLayout = {};
    bool s_combineLayoutReady = false;
    uint16_t s_width = 0;
    uint16_t s_height = 0;
    bgfx_texture_format_t s_colorFormat = BGFX_TEXTURE_FORMAT_RGBA16F;
    bgfx_texture_format_t s_depthFormat = BGFX_TEXTURE_FORMAT_D24;

    void DestroyTextures()
    {
        if (bgfxIsValid(s_hdrColor))
            bgfx_destroy_texture(s_hdrColor);
        if (bgfxIsValid(s_hdrDepth))
            bgfx_destroy_texture(s_hdrDepth);
        s_hdrColor = BGFX_INVALID_HANDLE;
        s_hdrDepth = BGFX_INVALID_HANDLE;
    }

    void DestroyCombineProgram()
    {
        if (bgfxIsValid(s_combineProgram))
            bgfx_destroy_program(s_combineProgram);
        if (bgfxIsValid(s_hdrSampler))
            bgfx_destroy_uniform(s_hdrSampler);
        if (bgfxIsValid(s_exposure))
            bgfx_destroy_uniform(s_exposure);
        s_combineProgram = BGFX_INVALID_HANDLE;
        s_hdrSampler = BGFX_INVALID_HANDLE;
        s_exposure = BGFX_INVALID_HANDLE;
        s_combineLayoutReady = false;
    }

    bool IsTextureSupported(bgfx_texture_format_t _format)
    {
        return bgfx_is_texture_valid(0, false, 1, _format,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP);
    }

    bool EnsureCombineProgram()
    {
        if (bgfxIsValid(s_combineProgram) && bgfxIsValid(s_hdrSampler) && bgfxIsValid(s_exposure))
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
            LogError("[BGFX] Combine program create failed");
            return false;
        }

        s_hdrSampler = bgfx_create_uniform("s_hdr", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_exposure = bgfx_create_uniform("u_exposure", BGFX_UNIFORM_TYPE_VEC4, 1);
        if (!bgfxIsValid(s_hdrSampler) || !bgfxIsValid(s_exposure))
        {
            LogError("[BGFX] Combine uniforms create failed");
            DestroyCombineProgram();
            return false;
        }

        LogInfo("[BGFX] Combine program created: %u", s_combineProgram.idx);
        return true;
    }
}

namespace bgfxHDR
{
    bool CreateHDRTarget(uint16_t _width, uint16_t _height)
    {
        if (_width == 0 || _height == 0)
            return false;

        if (bgfxIsValid(s_hdrFb) && s_width == _width && s_height == _height)
            return EnsureCombineProgram();

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

        if (!EnsureCombineProgram())
            return false;
        return true;
    }

    void DestroyHDRTarget()
    {
        if (bgfxIsValid(s_hdrFb))
            bgfx_destroy_frame_buffer(s_hdrFb);
        s_hdrFb = BGFX_INVALID_HANDLE;
        s_hdrColor = BGFX_INVALID_HANDLE;
        s_hdrDepth = BGFX_INVALID_HANDLE;
        s_width = 0;
        s_height = 0;
        DestroyCombineProgram();
    }

    bool RecreateOnResize(uint16_t _width, uint16_t _height)
    {
        if (bgfxIsValid(s_hdrFb) && s_width == _width && s_height == _height)
            return EnsureCombineProgram();
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

    bool CombinePass(uint16_t _width, uint16_t _height)
    {
        if (!IsReady() || !EnsureCombineProgram() || _width == 0 || _height == 0)
            return false;

        const float identity[16] =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        struct Vertex
        {
            float x, y, z, u, v;
        };
        // Fullscreen triangle covering the whole viewport (old verts covered
        // only half the screen, hypotenuse along the diagonal -> black half).
        // U=(x+1)/2, V=(1-y)/2 (V down, matches previous mapping); out-of-range
        // UVs are clamped by the HDR texture U_CLAMP|V_CLAMP flags.
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
        bgfx_set_view_transform(kCombineView, identity, identity);
        bgfx_touch(kCombineView);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_alloc_transient_vertex_buffer(&tvb, 3, &s_combineLayout);
        if (!tvb.data)
            return false;
        std::memcpy(tvb.data, vertices, sizeof(vertices));

        const float exposure[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, 0);
        bgfx_set_transient_vertex_buffer(0, &tvb, 0, 3);
        bgfx_set_texture(0, s_hdrSampler, s_hdrColor, 0);
        bgfx_set_uniform(s_exposure, exposure, 1);
        bgfx_submit(kCombineView, s_combineProgram, 0, BGFX_DISCARD_ALL);
        return true;
    }
}
