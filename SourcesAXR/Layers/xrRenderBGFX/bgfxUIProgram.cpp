// UI shader programs for bgfx.
// Shaders (.sc) are compiled at startup by the bgfx shaderc tool loaded as
// shaderc.dll (see bgfxShaderCompiler.cpp) for the active backend. The source
// files live in $game_shaders$ (ui_solid.*, ui_textured.*, hud_font.*).

#include "stdafx.h"
#include "bgfxUIProgram.h"
#include "bgfxShaderCompiler.h"
#include "bgfxRenderInterface.h"

#include <vector>

static bgfx_program_handle_t s_uiProgram = BGFX_INVALID_HANDLE;

bool bgfxUIProgramValid(bgfx_program_handle_t _h)
{
    return _h.idx != 0xFFFF;
}

namespace
{
    struct ShaderBlob
    {
        std::vector<uint8_t> bytes;
    };

    // Compiles the given .sc source for the active backend.
    ShaderBlob CompileStage(const char* _scFile, char _type)
    {
        ShaderBlob blob;
        bgfxShaderCompileFile(_scFile, _type, blob.bytes);
        return blob;
    }

    bgfx_shader_handle_t CreateStage(const ShaderBlob& _blob)
    {
        if (_blob.bytes.empty())
            return { 0xFFFF };
        const bgfx_memory_t* mem = bgfx_copy(_blob.bytes.data(), (u32)_blob.bytes.size());
        if (!mem)
            return { 0xFFFF };
        return bgfx_create_shader(mem);
    }

    bgfx_program_handle_t BuildProgram6x2(ShaderBlob _vs, ShaderBlob _ps)
    {
        bgfx_shader_handle_t vsh = CreateStage(_vs);
        bgfx_shader_handle_t fsh = CreateStage(_ps);
        if (!bgfxUIProgramValid(vsh) || !bgfxUIProgramValid(fsh))
            return BGFX_INVALID_HANDLE;

        return bgfx_create_program(vsh, fsh, true);
    }
}

static bgfx_program_handle_t s_uiTexProgram = BGFX_INVALID_HANDLE;
static bgfx_uniform_handle_t s_texSampler = BGFX_INVALID_HANDLE;
static bgfx_program_handle_t s_fontProgram = BGFX_INVALID_HANDLE;
static bgfx_texture_handle_t s_whiteTexture = BGFX_INVALID_HANDLE;

bgfx_program_handle_t bgfxUITexturedProgramGet()
{
    if (bgfxUIProgramValid(s_uiTexProgram))
        return s_uiTexProgram;

    ShaderBlob vs = CompileStage("ui_textured_vs.sc", 'v');
    ShaderBlob ps = CompileStage("ui_textured_ps.sc", 'f');
    s_uiTexProgram = BuildProgram6x2(vs, ps);
    if (!bgfxUIProgramValid(s_uiTexProgram))
        return BGFX_INVALID_HANDLE;

    s_texSampler = bgfx_create_uniform("u_texture", BGFX_UNIFORM_TYPE_SAMPLER, 1);
    LogInfo("[BGFX] Textured UI program created: %u", s_uiTexProgram.idx);
    return s_uiTexProgram;
}

bgfx_uniform_handle_t bgfxUITextureSamplerGet()
{
    bgfxUITexturedProgramGet(); // ensure created
    return s_texSampler;
}

bgfx_texture_handle_t bgfxUIWhiteTextureGet()
{
    if (bgfxIsValid(s_whiteTexture))
        return s_whiteTexture;

    u8 white[4] = { 255, 255, 255, 255 };
    const bgfx_memory_t* mem = bgfx_copy(white, 4);
    s_whiteTexture = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_RGBA8,
        BGFX_TEXTURE_NONE | BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_POINT, mem, 0);
    return s_whiteTexture;
}

bgfx_program_handle_t bgfxFontProgramGet()
{
    if (bgfxUIProgramValid(s_fontProgram))
        return s_fontProgram;

    ShaderBlob vs = CompileStage("hud_font_vs.sc", 'v');
    ShaderBlob ps = CompileStage("hud_font_ps.sc", 'f');
    s_fontProgram = BuildProgram6x2(vs, ps);
    if (!bgfxUIProgramValid(s_fontProgram))
        return BGFX_INVALID_HANDLE;

    bgfxUITextureSamplerGet(); // ensure sampler uniform exists
    LogInfo("[BGFX] Font program created: %u", s_fontProgram.idx);
    return s_fontProgram;
}

// Solid program: same VS layout as textured, PS outputs vertex color.
bgfx_program_handle_t bgfxUIProgramGet()
{
    if (bgfxUIProgramValid(s_uiProgram))
        return s_uiProgram;

    ShaderBlob vs = CompileStage("ui_solid_vs.sc", 'v');
    ShaderBlob ps = CompileStage("ui_solid_ps.sc", 'f');
    s_uiProgram = BuildProgram6x2(vs, ps);
    if (!bgfxUIProgramValid(s_uiProgram))
        return BGFX_INVALID_HANDLE;

    LogInfo("[BGFX] UI solid program created: %u", s_uiProgram.idx);
    return s_uiProgram;
}