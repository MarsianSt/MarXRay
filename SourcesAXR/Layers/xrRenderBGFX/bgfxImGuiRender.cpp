// bgfx ImGui output mirroring the ocornut_imgui example from bgfx-master.
// The precompiled shader bins (vs/fs_ocornut_imgui.bin.h) carry one array per
// backend; the Vulkan variant is used here (_spv).

#include "stdafx.h"
#include "bgfxImGuiRender.h"
#include "bgfx_capi.h"

#include <imgui.h>

#include "examples/common/imgui/vs_ocornut_imgui.bin.h"
#include "examples/common/imgui/fs_ocornut_imgui.bin.h"

namespace
{
    bgfx_program_handle_t s_program = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t  s_sampler = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t  s_fontTex = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t   s_vertexLayout;
    bool                   s_ready = false;

    bool IsValid(bgfx_handle_t h)                { return h.idx  != 0xFFFF; }

    struct ShaderBlob
    {
        const uint8_t* data;
        uint32_t size;
    };

    ShaderBlob PickVS()
    {
        switch (bgfx_get_renderer_type())
        {
        case BGFX_RENDERER_TYPE_DIRECT3D11: return ShaderBlob{ vs_ocornut_imgui_dxbc, sizeof(vs_ocornut_imgui_dxbc) };
        case BGFX_RENDERER_TYPE_DIRECT3D12: return ShaderBlob{ vs_ocornut_imgui_dxil, sizeof(vs_ocornut_imgui_dxil) };
        case BGFX_RENDERER_TYPE_OPENGL:     return ShaderBlob{ vs_ocornut_imgui_glsl, sizeof(vs_ocornut_imgui_glsl) };
        case BGFX_RENDERER_TYPE_METAL:      return ShaderBlob{ vs_ocornut_imgui_mtl,  sizeof(vs_ocornut_imgui_mtl) };
        case BGFX_RENDERER_TYPE_WEBGPU:     return ShaderBlob{ vs_ocornut_imgui_wgsl, sizeof(vs_ocornut_imgui_wgsl) };
        case BGFX_RENDERER_TYPE_VULKAN:
        default:                            return ShaderBlob{ vs_ocornut_imgui_spv,  sizeof(vs_ocornut_imgui_spv) };
        }
    }

    ShaderBlob PickFS()
    {
        switch (bgfx_get_renderer_type())
        {
        case BGFX_RENDERER_TYPE_DIRECT3D11: return ShaderBlob{ fs_ocornut_imgui_dxbc, sizeof(fs_ocornut_imgui_dxbc) };
        case BGFX_RENDERER_TYPE_DIRECT3D12: return ShaderBlob{ fs_ocornut_imgui_dxil, sizeof(fs_ocornut_imgui_dxil) };
        case BGFX_RENDERER_TYPE_OPENGL:     return ShaderBlob{ fs_ocornut_imgui_glsl, sizeof(fs_ocornut_imgui_glsl) };
        case BGFX_RENDERER_TYPE_METAL:      return ShaderBlob{ fs_ocornut_imgui_mtl,  sizeof(fs_ocornut_imgui_mtl) };
        case BGFX_RENDERER_TYPE_WEBGPU:     return ShaderBlob{ fs_ocornut_imgui_wgsl, sizeof(fs_ocornut_imgui_wgsl) };
        case BGFX_RENDERER_TYPE_VULKAN:
        default:                            return ShaderBlob{ fs_ocornut_imgui_spv,  sizeof(fs_ocornut_imgui_spv) };
        }
    }
}

bool bgfxImguiInit()
{
    if (s_ready)
        return true;

    if (!ImGui::GetCurrentContext())
        return false;

    ImGuiIO& io = ImGui::GetIO();

    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    if (!pixels || w <= 0 || h <= 0)
    {
        LogError("[BGFX] ImGui font atlas build failed");
        return false;
    }

    const bgfx_memory_t* mem = bgfx_copy(pixels, (uint32_t)w * (uint32_t)h * 4);
    s_fontTex = bgfx_create_texture_2d(
        (uint16_t)w, (uint16_t)h, false, 1, BGFX_TEXTURE_FORMAT_RGBA8,
        BGFX_TEXTURE_NONE | BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_POINT, mem, 0);
    if (!IsValid(s_fontTex))
    {
        LogError("[BGFX] ImGui font texture create failed");
        return false;
    }

    ShaderBlob vsBlob = PickVS();
    ShaderBlob fsBlob = PickFS();
    bgfx_shader_handle_t vsh = bgfx_create_shader(bgfx_copy(vsBlob.data, vsBlob.size));
    bgfx_shader_handle_t fsh = bgfx_create_shader(bgfx_copy(fsBlob.data, fsBlob.size));
    if (!IsValid(vsh) || !IsValid(fsh))
    {
        LogError("[BGFX] ImGui shader create failed (renderer %d)", (int)bgfx_get_renderer_type());
        return false;
    }

    s_program = bgfx_create_program(vsh, fsh, true);
    if (!IsValid(s_program))
    {
        LogError("[BGFX] ImGui program create failed");
        return false;
    }

    s_sampler = bgfx_create_uniform("s_tex", BGFX_UNIFORM_TYPE_SAMPLER, 1);
    if (!IsValid(s_sampler))
    {
        LogError("[BGFX] ImGui sampler create failed");
        return false;
    }

    bgfx_vertex_layout_begin(&s_vertexLayout, bgfx_get_renderer_type());
    bgfx_vertex_layout_add(&s_vertexLayout, BGFX_ATTRIB_POSITION, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
    bgfx_vertex_layout_add(&s_vertexLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
    bgfx_vertex_layout_add(&s_vertexLayout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
    bgfx_vertex_layout_end(&s_vertexLayout);

    s_ready = true;
    LogInfo("[BGFX] ImGui bgfx renderer ready (font atlas %dx%d)", w, h);
    return true;
}

void bgfxImguiRenderFrame()
{
    if (!s_ready)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || !dd->Valid || dd->CmdListsCount <= 0)
        return;

    const int fbWidth  = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    const int fbHeight = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fbWidth <= 0 || fbHeight <= 0)
        return;

    const bgfx_caps_t* caps = bgfx_get_caps();
    const bool homogeneousDepth = caps && caps->homogeneousDepth;

    bgfx_set_view_rect(5, 0, 0, (uint16_t)fbWidth, (uint16_t)fbHeight);
    bgfx_set_view_clear(5, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(5, BGFX_VIEW_MODE_SEQUENTIAL);

    // Orthographic projection (top-left origin, pixel space), parallel to
    // bx::mtxOrtho as used by the bgfx imgui example.
    float ortho[16] = { 0.0f };
    const float l = 0.0f, r = (float)fbWidth, t = 0.0f, b = (float)fbHeight;
    const float n = 0.0f, f = 1000.0f;
    const float aa = 2.0f / (r - l);
    const float bb = 2.0f / (t - b);
    if (homogeneousDepth)
    {
        const float cc = 1.0f / (f - n);
        ortho[0] = aa;  ortho[5] = bb;  ortho[10] = -cc; ortho[15] = 1.0f;
        ortho[12] = -(l + r) * aa * 0.5f;
        ortho[13] = -(t + b) * bb * 0.5f;
        ortho[14] = n * cc;
    }
    else
    {
        const float cc = 2.0f / (f - n);
        ortho[0] = aa;  ortho[5] = bb;  ortho[10] = cc; ortho[15] = 1.0f;
        ortho[12] = -(l + r) * aa * 0.5f;
        ortho[13] = -(t + b) * bb * 0.5f;
        ortho[14] = -(f + n) * cc * 0.5f;
    }

    float ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    bgfx_set_view_transform(5, ident, ortho);
    bgfx_touch(5);

    const float sx = io.DisplayFramebufferScale.x;
    const float sy = io.DisplayFramebufferScale.y;

    for (int nlist = 0; nlist < dd->CmdListsCount; ++nlist)
    {
        const ImDrawList* cl = dd->CmdLists[nlist];
        if (!cl || cl->CmdBuffer.Size <= 0)
            continue;

        const int nVert = cl->VtxBuffer.Size;
        const int nIdx  = cl->IdxBuffer.Size;
        if (nVert <= 0 || nIdx <= 0)
            continue;

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_transient_index_buffer_t tib;
        bgfx_alloc_transient_vertex_buffer(&tvb, (uint32_t)nVert, &s_vertexLayout);
        bgfx_alloc_transient_index_buffer(&tib, (uint32_t)nIdx, false);
        if (!tvb.data || !tib.data)
            continue;

        memcpy(tvb.data, cl->VtxBuffer.begin(), (size_t)nVert * sizeof(ImDrawVert));
        memcpy(tib.data, cl->IdxBuffer.begin(), (size_t)nIdx * sizeof(ImDrawIdx));

        for (int c = 0; c < cl->CmdBuffer.Size; ++c)
        {
            const ImDrawCmd* cmd = &cl->CmdBuffer[c];
            if (cmd->UserCallback)
            {
                cmd->UserCallback(cl, cmd);
                continue;
            }
            if (cmd->ElemCount == 0)
                continue;

            // Clip rect -> framebuffer pixel rectangle.
            const ImVec4& cr = cmd->ClipRect;
            int32_t x0 = (int32_t)((cr.x - dd->DisplayPos.x) * sx);
            int32_t y0 = (int32_t)((cr.y - dd->DisplayPos.y) * sy);
            int32_t x1 = (int32_t)(ceilf((cr.z - dd->DisplayPos.x) * sx));
            int32_t y1 = (int32_t)(ceilf((cr.w - dd->DisplayPos.y) * sy));
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > fbWidth)  x1 = fbWidth;
            if (y1 > fbHeight) y1 = fbHeight;
            if (x1 <= x0 || y1 <= y0)
                continue;

            uint16_t scissorCache = bgfx_set_scissor((uint16_t)x0, (uint16_t)y0,
                (uint16_t)(x1 - x0), (uint16_t)(y1 - y0));
            bgfx_set_scissor_cached(scissorCache);

            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                           | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;
            bgfx_set_state(state, 0);
            bgfx_set_texture(0, s_sampler, s_fontTex, UINT32_MAX);

            bgfx_set_transient_vertex_buffer(0, &tvb, (uint32_t)cmd->VtxOffset, (uint32_t)cmd->ElemCount);
            bgfx_set_transient_index_buffer(&tib, (uint32_t)cmd->IdxOffset, (uint32_t)cmd->ElemCount);

            bgfx_submit(5, s_program, 0, BGFX_DISCARD_ALL);
        }
    }
}