// World shader programs: flat-shaded static geometry for the BGFX port's
// world-render pass. Level camera matrices are placed on view 0 by the
// render device every frame (SetCacheXform), so the vertex shader only needs
// the built-in u_modelViewProj uniform (pre-multiplied proj*view*model,
// model = identity for level geometry).
//
// Shaders (.sc) are compiled at startup by the bgfx shaderc tool loaded as
// shaderc.dll (see bgfxShaderCompiler.cpp) for the active backend. The source
// files live in $game_shaders$ (world_solid.*, world_decal.vs,
// world_terrain.*). Uniform entries (u_modelViewProj, u_alphaCtrl,
// u_detailCtrl, samplers) are baked into the compiled blobs.

#include "stdafx.h"
#include "bgfxWorldProgram.h"
#include "bgfxShaderCompiler.h"

#include <cstring>
#include <vector>

static bgfx_program_handle_t s_worldProgram = BGFX_INVALID_HANDLE;
static bgfx_program_handle_t s_worldDecalProgram = BGFX_INVALID_HANDLE;
static bgfx_program_handle_t s_worldTerrainProgram = BGFX_INVALID_HANDLE;
static bgfx_program_handle_t s_skinProgram[4] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
						  BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };

static bool s_valid(bgfx_program_handle_t h) { return h.idx != 0xFFFF; }

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

    bgfx_program_handle_t BuildProgram(ShaderBlob _vs, ShaderBlob _ps)
    {
        bgfx_shader_handle_t vsh = CreateStage(_vs);
        bgfx_shader_handle_t fsh = CreateStage(_ps);
        if (!s_valid(vsh) || !s_valid(fsh))
            return BGFX_INVALID_HANDLE;

        return bgfx_create_program(vsh, fsh, true);
    }
}

bgfx_program_handle_t bgfxWorldProgramGet()
{
    if (s_valid(s_worldProgram))
        return s_worldProgram;

    ShaderBlob vs = CompileStage("world_solid_vs.sc", 'v');
    ShaderBlob ps = CompileStage("world_solid_ps.sc", 'f');
    s_worldProgram = BuildProgram(vs, ps);
    if (s_valid(s_worldProgram))
        LogInfo("[BGFX] World program created: %u", s_worldProgram.idx);
    return s_worldProgram;
}

bgfx_program_handle_t bgfxWorldDecalProgramGet()
{
    if (s_valid(s_worldDecalProgram))
        return s_worldDecalProgram;

    ShaderBlob vs = CompileStage("world_decal_vs.sc", 'v');
    ShaderBlob ps = CompileStage("world_solid_ps.sc", 'f');
    s_worldDecalProgram = BuildProgram(vs, ps);
    if (s_valid(s_worldDecalProgram))
        LogInfo("[BGFX] World decal program created: %u", s_worldDecalProgram.idx);
    return s_worldDecalProgram;
}

bgfx_program_handle_t bgfxWorldTerrainProgramGet()
{
    if (s_valid(s_worldTerrainProgram))
        return s_worldTerrainProgram;

    ShaderBlob vs = CompileStage("world_terrain_vs.sc", 'v');
    ShaderBlob ps = CompileStage("world_terrain_ps.sc", 'f');
    s_worldTerrainProgram = BuildProgram(vs, ps);
    if (s_valid(s_worldTerrainProgram))
        LogInfo("[BGFX] World terrain program created: %u", s_worldTerrainProgram.idx);
    return s_worldTerrainProgram;
}

// Skinned models (vertHW_1W..4W, SkeletonX._Load_hw) index their bones through
// u_bones (3 rows per bone, see SkeletonX::_Render matrix packing).
bgfx_program_handle_t bgfxSkinProgramGet(uint32_t mode)
{
    if (mode > 3)
        return BGFX_INVALID_HANDLE;
    if (s_valid(s_skinProgram[mode]))
        return s_skinProgram[mode];

    static const char* vsNames[4] = { "skin1w_vs.sc", "skin2w_vs.sc", "skin3w_vs.sc", "skin4w_vs.sc" };
    ShaderBlob vs = CompileStage(vsNames[mode], 'v');
    ShaderBlob ps = CompileStage("skin_ps.sc", 'f');
    s_skinProgram[mode] = BuildProgram(vs, ps);
    if (s_valid(s_skinProgram[mode]))
        LogInfo("[BGFX] Skin program created: mode=%u id=%u", (u32)mode, s_skinProgram[mode].idx);
    return s_skinProgram[mode];
}