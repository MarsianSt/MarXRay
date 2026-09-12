#pragma once

#include <vector>
#include <cstdint>

// Runtime shader compiler bridge to shaderc.dll (the bgfx shaderc tool built
// as a DLL). Shaders (.sc) are compiled at startup for the active bgfx
// backend; there is no on-disk shader cache.
//
// _scFile is a path relative to $game_shaders$ (e.g. "ui_solid_vs.sc").
// On success the compiled bgfx binary shader blob is returned in _outBlob.
// Returns false if shaderc.dll is unavailable or compilation fails.
bool bgfxShaderCompileFile(const char* _scFile, char _type, std::vector<std::uint8_t>& _outBlob);