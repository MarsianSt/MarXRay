#pragma once

// World (static level geometry) shader programs.
// Shaders are precompiled off-line with shaderc into backend-specific blobs
// (dxbc/dxil/glsl/spirv) embedded as arrays; the active backend is picked
// at runtime. Exposed through extern "C" so the port TU (bgfxRenderCompat.cpp)
// can use them.

#include "bgfx_capi.h"

extern "C"
{
	bgfx_program_handle_t bgfxWorldProgramGet();
	bgfx_program_handle_t bgfxWorldDecalProgramGet();
	bgfx_program_handle_t bgfxWorldTerrainProgramGet();

	// Skinned (CKinematics) programs; mode: 0=1W, 1=2W, 2=3W, 3=4W weights.
	bgfx_program_handle_t bgfxSkinProgramGet(uint32_t mode);
}
