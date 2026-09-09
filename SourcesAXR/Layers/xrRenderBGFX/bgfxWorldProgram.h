#pragma once

// World (static level geometry) shader program built with the same
// D3DCompile + bgfx-wrap technique as the UI programs (bgfxUIProgram.cpp).
// Exposed through extern "C" so the port TU (bgfxRenderCompat.cpp) can use
// it without pulling in d3dcompiler / its ID3DBlob (which clashes with the
// port's stub ID3DBlob).

#include "bgfx_capi.h"

extern "C"
{
	bgfx_program_handle_t bgfxWorldProgramGet();
	bgfx_program_handle_t bgfxWorldDecalProgramGet();
}
