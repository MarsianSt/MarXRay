#ifndef bgfxD3DX_included
#define bgfxD3DX_included
#pragma once

// ============================================================================
// BGFX port: replacement for <d3dx9.h> / d3dx9 API usage in the ported visual
// files. Provides the handful of D3D declaration constants/methods that the
// copy-pasted code still references, plus a dx10BufferUtils-compatible facade.
// ============================================================================

#include "bgfxVisualTypes.h"
#include "bgfxVBMacros.h"
#include "bgfxGpuCompat.h"

// D3D_PRIMITIVE enums used as D3DPRIMITIVETYPE
typedef u32 D3DPRIMITIVETYPE;
typedef u32 D3DCAPS9;
typedef u32 D3DSAMPLERSTATETYPE;
typedef u32 D3DRENDERSTATETYPE;
typedef u32 D3DSTENCILOP;
typedef u32 D3DCMPFUNC;
typedef u32 D3DCOLORWRITEENABLE;

// dx10BufferUtils facade
namespace dx10BufferUtils
{
	inline HRESULT CreateVertexBuffer(ID3DVertexBuffer** ppvb, const void* data, u32 /*size*/)
	{
		// data is raw bytes; stride/count are not known here - the caller sets
		// them via the buffer's own vStride/vCount in the model code.
		*ppvb = bgfxCreateVertexBuffer(data, 0, 0);
		if (*ppvb)
			(*ppvb)->vCount = 1;
		return S_OK;
	}

	inline HRESULT CreateIndexBuffer(ID3DIndexBuffer** ppib, const void* data, u32 /*size*/)
	{
		*ppib = bgfxCreateIndexBuffer(data, 0);
		if (*ppib)
			(*ppib)->iCount = 1;
		return S_OK;
	}
} // namespace dx10BufferUtils

// d3dx9 helpers used by the visual loaders (all no-ops / trivial)
#define D3DXDeclaratorFromFVF(fvf, dcl)		::bgfxD3DFVFTriangleListDeclaratorFromFVF((fvf),(dcl))
#define D3DXGetFVFVertexSize(fvf)			::bgfxD3DFVFVertexSize((fvf))

// D3DXGetDeclVertexSize: size in bytes of one vertex for a declaration
inline u32 bgfxD3DDeclVertexSize(const D3DVERTEXELEMENT9* dcl, u32 stream)
{
	u32 wordStrideCache = 0;
	u32 maxOffset = 0;
	for (u32 i = 0; dcl[i].Stream != 0xFF && dcl[i].Type != D3DDECLTYPE_UNUSED; ++i)
	{
		if (dcl[i].Stream != stream)
			continue;
		u32 sz = 0;
		switch (dcl[i].Type)
		{
		case D3DDECLTYPE_FLOAT1: sz = 4; break;
		case D3DDECLTYPE_FLOAT2: sz = 8; break;
		case D3DDECLTYPE_FLOAT3: sz = 12; break;
		case D3DDECLTYPE_FLOAT4: sz = 16; break;
		case D3DDECLTYPE_D3DCOLOR: sz = 4; break;
		case D3DDECLTYPE_UBYTE4: sz = 4; break;
		case D3DDECLTYPE_UBYTE4N: sz = 4; break;
		case D3DDECLTYPE_SHORT2: sz = 4; break;
		case D3DDECLTYPE_SHORT2N: sz = 4; break;
		case D3DDECLTYPE_SHORT4: sz = 8; break;
		case D3DDECLTYPE_SHORT4N: sz = 8; break;
		case D3DDECLTYPE_USHORT2N: sz = 4; break;
		case D3DDECLTYPE_USHORT4N: sz = 8; break;
		case D3DDECLTYPE_FLOAT16_2: sz = 4; break;
		case D3DDECLTYPE_FLOAT16_4: sz = 8; break;
		case D3DDECLTYPE_UDEC3: sz = 4; break;
		case D3DDECLTYPE_DEC3N: sz = 4; break;
		default: sz = 4; break;
		}
		maxOffset = std::max(maxOffset, (u32)(dcl[i].Offset + sz));
	}
	return maxOffset;
}
#define D3DXGetDeclVertexSize(dcl, stream)	::bgfxD3DDeclVertexSize((dcl),(stream))

// D3DXGetDeclLength: number of active elements (excluding the D3DDECL_END terminator)
inline u32 bgfxD3DDeclLength(const D3DVERTEXELEMENT9* dcl)
{
	u32 n = 0;
	while (n < MAX_FVF_DECL_SIZE && dcl[n].Stream != 0xFF && dcl[n].Type != D3DDECLTYPE_UNUSED)
		++n;
	return n;
}
#define D3DXGetDeclLength(dcl) ::bgfxD3DDeclLength((dcl))

#endif // bgfxD3DX_included