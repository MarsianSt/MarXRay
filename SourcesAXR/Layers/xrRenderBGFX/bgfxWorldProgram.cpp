// World shader program: flat-shaded static geometry for the BGFX port's
// world-render pass. Level camera matrices are placed on view 0 by the
// render device every frame (SetCacheXform), so the vertex shader only needs
// the built-in u_modelViewProj uniform (pre-multiplied proj*view*model,
// model = identity for level geometry).
//
// The shader sources live in <gamedata>\shaders (world_solid.vs/.ps), loaded
// through the engine VFS ($game_shaders$) and compiled at runtime with
// D3DCompile (vs_5_0/ps_5_0). The DXBC blobs are wrapped into the bgfx binary
// shader format (magic VSH/FSH). Format (bgfx_src/renderer_d3d11.cpp
// ShaderD3D11::create):
//   u32 magic ('V','S','H',ver=12) / ('F','S','H',12)
//   u32 hashIn, u32 hashOut, u32 srvMask, u32 uavMask
//   u16 uniformCount (VS: 1 predefined, PS: 0)
//   uniform entries (only VS): u8 nameSize, name, u8 type, u8 num,
//     u16 regIndex, u16 regCount, u8 texComponent, u8 texDimension,
//     u16 texFormat (ver>=8/>=10 fields)
//   u32 bytecodeSize, u8 bytecode[bytecodeSize], u8 terminator(0)
//   u8  numAttrs (VS: 1, PS: 0), u16 attrIds[]
//   u16 cbufferSize (VS: 64 bytes = 16 float4s, PS: 0)
//
// u_modelViewProj maps to bgfx PredefinedUniform::ModelViewProj (11), so the
// D3D11 renderer feeds proj*view per draw from the frame cache; the trailing
// cbufferSize > 0 makes ShaderD3D11::create allocate the per-shader constant
// buffer (m_buffer) that commitShaderConstants() updates from m_vsScratch.

#include "stdafx.h"
#include "bgfxWorldProgram.h"

#include <d3dcompiler.h>
#include <cstring>

static bgfx_program_handle_t s_worldProgram = BGFX_INVALID_HANDLE;
static bgfx_program_handle_t s_worldDecalProgram = BGFX_INVALID_HANDLE;

static bool s_valid(bgfx_program_handle_t h) { return h.idx != 0xFFFF; }

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

// Compiles one stage, wraps DXBC into the bgfx shader format, and embeds the
// predefined u_modelViewProj uniform entry on the vertex stage.
static bgfx_shader_handle_t CompileWorldShader(bool _isVS, const char* srcText, const char* fname)
{
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(srcText, (SIZE_T)strlen(srcText), fname, nullptr, nullptr,
                            "main", _isVS ? "vs_5_0" : "ps_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr))
    {
        LogError("[BGFX] World shader compile failed '%s': %s",
            fname, err ? (LPCSTR)err->GetBufferPointer() : "unknown error");
        if (err) err->Release();
        return BGFX_INVALID_HANDLE;
    }
    if (err) err->Release();

    const u32 magic    = _isVS ? 0x0C485356u : 0x0C485346u; // 'V','S','H',12 / 'F','S','H',12
    const u32 codeSz   = (u32)code->GetBufferSize();
    const u32 numAttr  = _isVS ? 2u : 0u;
    const u32 unifBlob = _isVS
        ? (1 + 15 + 1 + 1 + 2 + 2 + 1 + 1 + 2)   // u_modelViewProj Mat4
        : (1 + 11 + 1 + 1 + 2 + 2 + 1 + 1 + 2);  // u_alphaCtrl Vec4
    const u32 total    = 4 + 4 + 4 + 4 + 4 + 2 + unifBlob + 4 + codeSz + 1 + 1 + numAttr * 2 + 2;

    const bgfx_memory_t* mem = bgfx_alloc(total);
    if (!mem || !mem->data)
    {
        LogError("[BGFX] bgfx_alloc failed for world shader binary (%u bytes)", total);
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
    Write16(1); // uniform count

    if (_isVS)
    {
        // Predefined: nameToPredefinedUniformEnum("u_modelViewProj") -> ModelViewProj.
        // UniformType::Mat4 = 4 (Sampler=0, End=1, Vec4=2, Mat3=3, Mat4=4).
        Write8(15);
        memcpy(d, "u_modelViewProj", 15); d += 15;
        Write8(4);   // type = Mat4
        Write8(1);   // num
        Write16(0);  // regIndex (HLSL binding c0)
        Write16(4);  // regCount (4x4)
        Write8(0);   // texComponent (ver>=8)
        Write8(0);   // texDimension (ver>=8)
        Write16(0);  // texFormat (ver>=10)

    }
    else
    {
        // Pixel-stage alpha control: x=alphaRef, y=enableAlphaTest.
        Write8(11);
        memcpy(d, "u_alphaCtrl", 11); d += 11;
        Write8(2);   // type = Vec4
        Write8(1);   // num
        Write16(0);  // regIndex (HLSL binding c0)
        Write16(1);  // regCount
        Write8(0);
        Write8(0);
        Write16(0);
    }

    Write32(codeSz);
    memcpy(d, code->GetBufferPointer(), codeSz);
    d += codeSz;
    Write8(0); // terminator
    Write8((u8)numAttr);
    if (_isVS)
    {
        Write16(0x0001);  // Position (bgfx idToAttrib: Position = 0x0001)
        Write16(0x0010);  // TexCoord0 (bgfx idToAttrib: TexCoord0 = 0x0010)
    }
    Write16(_isVS ? 64 : 16); // cbuffer size (bytes)

    code->Release();

    return bgfx_create_shader(mem);
}

static bgfx_shader_handle_t BuildShader(bool _isVS, const char* fname)
{
    char* src = nullptr;
    if (!LoadShaderSource(fname, src))
    {
        LogError("[BGFX] World shader source not found: $game_shaders$\\%s", fname);
        return BGFX_INVALID_HANDLE;
    }

    bgfx_shader_handle_t h = CompileWorldShader(_isVS, src, fname);
    xr_free(src);
    return h;
}

bgfx_program_handle_t bgfxWorldProgramGet()
{
    if (s_valid(s_worldProgram))
        return s_worldProgram;

    bgfx_shader_handle_t vsh = BuildShader(true, "world_solid.vs");
    bgfx_shader_handle_t fsh = BuildShader(false, "world_solid.ps");
    if (!s_valid(vsh) || !s_valid(fsh))
        return BGFX_INVALID_HANDLE;

    s_worldProgram = bgfx_create_program(vsh, fsh, true);
    if (s_valid(s_worldProgram))
        LogInfo("[BGFX] World program created: %u", s_worldProgram.idx);
    return s_worldProgram;
}

bgfx_program_handle_t bgfxWorldDecalProgramGet()
{
    if (s_valid(s_worldDecalProgram))
        return s_worldDecalProgram;

    bgfx_shader_handle_t vsh = BuildShader(true, "world_decal.vs");
    bgfx_shader_handle_t fsh = BuildShader(false, "world_solid.ps");
    if (!s_valid(vsh) || !s_valid(fsh))
        return BGFX_INVALID_HANDLE;

    s_worldDecalProgram = bgfx_create_program(vsh, fsh, true);
    if (s_valid(s_worldDecalProgram))
        LogInfo("[BGFX] World decal program created: %u", s_worldDecalProgram.idx);
    return s_worldDecalProgram;
}
