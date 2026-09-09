// UI shader programs for bgfx D3D11.
// Shader sources live in <gamedata>\shaders (ui_solid.*, ui_textured.*,
// hud_font.*), loaded through the engine VFS ($game_shaders$) and compiled
// at runtime with D3DCompile (vs_5_0/ps_5_0). The DXBC blobs are wrapped
// into the bgfx binary shader format (magic VSH/FSH + header) so that
// bgfx_create_shader accepts them. Format (see bgfx_src/renderer_d3d11.cpp
// ShaderD3D11::create):
//   u32 magic ('V','S','H',ver=12) / ('F','S','H',12)
//   u32 hashIn, u32 hashOut, u32 srvMask, u32 uavMask
//   u16 uniformCount (0)
//   u32 bytecodeSize, u8 bytecode[bytecodeSize], u8 terminator(0)
//   u8  numAttrs (VS: 3, PS: 0), u16 attrIds[]
//   u16 cbufferSize (0)

#include "stdafx.h"
#include "bgfxUIProgram.h"
#include "bgfxRenderInterface.h"

#include <d3dcompiler.h>

static bgfx_program_handle_t s_uiProgram = BGFX_INVALID_HANDLE;

bool bgfxUIProgramValid(bgfx_program_handle_t _h)
{
    return _h.idx != 0xFFFF;
}

// Loads shader source text from $game_shaders$ (gamedata\shaders).
// Returns a NUL-terminated string allocated with xr_malloc; caller frees.
static bool LoadShaderSource(const char* fname, char*& outText)
{
    outText = nullptr;
    IReader* file = FS.r_open("$game_shaders$", fname);
    if (!file)
        return false;

    u32 size = (u32)file->elapsed();
    outText = (char*)xr_malloc(size + 1);
    file->r(outText, (int)size);
    outText[size] = 0;
    FS.r_close(file);
    return true;
}

// Compiles one stage and wraps the DXBC into the bgfx shader format.
static bgfx_shader_handle_t CompileShader(bool _isVS, const char* srcText, const char* fname)
{
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(srcText, (SIZE_T)strlen(srcText), fname, nullptr, nullptr,
                            "main", _isVS ? "vs_5_0" : "ps_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr))
    {
        LogError("[BGFX] Shader compile failed '%s': %s",
            fname, err ? (LPCSTR)err->GetBufferPointer() : "unknown error");
        if (err) err->Release();
        return BGFX_INVALID_HANDLE;
    }
    if (err) err->Release();

    const u32 magic   = _isVS ? 0x0C485356u : 0x0C485346u; // 'V','S','H',12 / 'F','S','H',12
    const u32 codeSz  = (u32)code->GetBufferSize();
    const u32 numAttr = _isVS ? 3u : 0u;
    const u32 total   = 4 + 4 + 4 + 4 + 4 + 2 + 4 + codeSz + 1 + 1 + numAttr * 2 + 2;

    const bgfx_memory_t* mem = bgfx_alloc(total);
    if (!mem || !mem->data)
    {
        LogError("[BGFX] bgfx_alloc failed for shader binary (%u bytes)", total);
        code->Release();
        return BGFX_INVALID_HANDLE;
    }

    u8* d = (u8*)mem->data;
    auto Write32 = [&d](u32 v) { memcpy(d, &v, 4); d += 4; };
    auto Write16 = [&d](u16 v) { memcpy(d, &v, 2); d += 2; };
    auto Write8  = [&d](u8 v)  { *d = v; d += 1; };

    Write32(magic);
    Write32(0); // hashIn
    Write32(0); // hashOut
    Write32(0); // srvMask
    Write32(0); // uavMask
    Write16(0); // uniform count
    Write32(codeSz);
    memcpy(d, code->GetBufferPointer(), codeSz);
    d += codeSz;
    Write8(0); // terminator
    Write8((u8)numAttr);
    if (_isVS)
    {
        // Attribute ids must match bgfx_src/vertexlayout.cpp s_attribToId
        // (idToAttrib table): Position=0x0001, Color0=0x0005, TexCoord0=0x0010.
        Write16(0x0001);  // Position
        Write16(0x0005);  // Color0
        Write16(0x0010);  // TexCoord0
    }
    Write16(0); // cbuffer size

    code->Release();

    return bgfx_create_shader(mem);
}

// Loads, compiles and wraps one stage from a gamedata\shaders file.
static bgfx_shader_handle_t BuildShader(bool _isVS, const char* fname)
{
    char* src = nullptr;
    if (!LoadShaderSource(fname, src))
    {
        LogError("[BGFX] Shader source not found: $game_shaders$\\%s", fname);
        return BGFX_INVALID_HANDLE;
    }

    bgfx_shader_handle_t h = CompileShader(_isVS, src, fname);
    xr_free(src);
    return h;
}

static bgfx_program_handle_t BuildProgram(const char* vsName, const char* psName)
{
    bgfx_shader_handle_t vsh = BuildShader(true, vsName);
    bgfx_shader_handle_t fsh = BuildShader(false, psName);
    if (!bgfxUIProgramValid(vsh) || !bgfxUIProgramValid(fsh))
        return BGFX_INVALID_HANDLE;

    return bgfx_create_program(vsh, fsh, true);
}

static bgfx_program_handle_t s_uiTexProgram = BGFX_INVALID_HANDLE;
static bgfx_uniform_handle_t s_texSampler = BGFX_INVALID_HANDLE;
static bgfx_program_handle_t s_fontProgram = BGFX_INVALID_HANDLE;
static bgfx_texture_handle_t s_whiteTexture = BGFX_INVALID_HANDLE;

bgfx_program_handle_t bgfxUITexturedProgramGet()
{
    if (bgfxUIProgramValid(s_uiTexProgram))
        return s_uiTexProgram;

    s_uiTexProgram = BuildProgram("ui_textured.vs", "ui_textured.ps");
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

    s_fontProgram = BuildProgram("hud_font.vs", "hud_font.ps");
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

    s_uiProgram = BuildProgram("ui_solid.vs", "ui_solid.ps");
    if (!bgfxUIProgramValid(s_uiProgram))
        return BGFX_INVALID_HANDLE;

    LogInfo("[BGFX] UI solid program created: %u", s_uiProgram.idx);
    return s_uiProgram;
}
