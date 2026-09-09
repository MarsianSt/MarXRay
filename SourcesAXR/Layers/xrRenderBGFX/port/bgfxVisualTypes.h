#ifndef bgfxVisualTypes_included
#define bgfxVisualTypes_included
#pragma once

// ****************************************************************************
// *  BGFX-compatible stand-in for the Direct3D types used by the ported      *
// *  xrRender visual classes (FBasicVisual, FVisual, FSkinned, SkeletonX...).*
// *                                                                          *
// *  Instead of real D3D9/D3D11 buffers we create our own light-weight       *
// *  vertex/index buffer holders. Vertex/Index data is uploaded to bgfx via  *
// *  bgfxCreateVertexBuffer / bgfxCreateIndexBuffer helpers.                 *
// ****************************************************************************

#include <vector>
#include <cstring>

#define BGFX_GEOMETRY_ALIGN 16u

// ----------------------------------------------------------------------------
// Vertex declaration element (DX9-style, used all over xrRender)
// ----------------------------------------------------------------------------
struct D3DVERTEXELEMENT9
{
	WORD Stream;
	WORD Offset;
	BYTE Type;
	BYTE Method;
	BYTE Usage;
	BYTE UsageIndex;
};

struct D3DVERTEXELEMENT9_END
{
	WORD Stream;
	WORD Offset;
	BYTE Type;
	BYTE Method;
	BYTE Usage;
	BYTE UsageIndex;
};
#define D3DDECL_END() 0xFF

// D3DDECLMETHOD (declaration method)
#define D3DDECLMETHOD_DEFAULT		0
#define D3DDECLMETHOD_PARTIALU		1
#define D3DDECLMETHOD_PARTIALV		2
#define D3DDECLMETHOD_CROSSUV		3
#define D3DDECLMETHOD_UV			4
#define D3DDECLMETHOD_LOOKUP		5
#define D3DDECLMETHOD_LOOKUPPRESAMPLED 6

// D3DDECLTYPE
#define D3DDECLTYPE_FLOAT1		0
#define D3DDECLTYPE_FLOAT2		1
#define D3DDECLTYPE_FLOAT3		2
#define D3DDECLTYPE_FLOAT4		3
#define D3DDECLTYPE_D3DCOLOR	4
#define D3DDECLTYPE_UBYTE4		5
#define D3DDECLTYPE_SHORT2		6
#define D3DDECLTYPE_SHORT4		7
#define D3DDECLTYPE_UBYTE4N		8
#define D3DDECLTYPE_SHORT2N		9
#define D3DDECLTYPE_SHORT4N		10
#define D3DDECLTYPE_USHORT2N	11
#define D3DDECLTYPE_USHORT4N	12
#define D3DDECLTYPE_UDEC3		13
#define D3DDECLTYPE_DEC3N		14
#define D3DDECLTYPE_FLOAT16_2	15
#define D3DDECLTYPE_FLOAT16_4	16
#define D3DDECLTYPE_UNUSED		17

// D3DDECLUSAGE
#define D3DDECLUSAGE_POSITION		0
#define D3DDECLUSAGE_BLENDWEIGHT	1
#define D3DDECLUSAGE_BLENDINDICES	2
#define D3DDECLUSAGE_NORMAL			3
#define D3DDECLUSAGE_PSIZE			4
#define D3DDECLUSAGE_TEXCOORD		5
#define D3DDECLUSAGE_TANGENT		6
#define D3DDECLUSAGE_BINORMAL		7
#define D3DDECLUSAGE_TESSFACTOR		8
#define D3DDECLUSAGE_POSITIONT		9
#define D3DDECLUSAGE_COLOR			10
#define D3DDECLUSAGE_FOG			11
#define D3DDECLUSAGE_DEPTH			12
#define D3DDECLUSAGE_SAMPLE			13

// FVF (Flexible Vertex Format) bits
#define D3DFVF_RESERVED0	0x001
#define D3DFVF_XYZ		0x002
#define D3DFVF_XYZRHW		0x004
#define D3DFVF_XYZB1		0x006
#define D3DFVF_XYZB2		0x008
#define D3DFVF_XYZB3		0x00a
#define D3DFVF_XYZB4		0x00c
#define D3DFVF_XYZB5		0x00e
#define D3DFVF_XYZW		0x4000
#define D3DFVF_NORMAL		0x010
#define D3DFVF_PSIZE		0x020
#define D3DFVF_DIFFUSE		0x040
#define D3DFVF_SPECULAR		0x080
#define D3DFVF_TEX1		0x100
#define D3DFVF_TEX2		0x200
#define D3DFVF_TEX3		0x300
#define D3DFVF_TEX4		0x400
#define D3DFVF_TEX5		0x500
#define D3DFVF_TEX6		0x600
#define D3DFVF_TEX7		0x700
#define D3DFVF_TEX8		0x800
#define D3DFVF_LASTBETA_UBYTE4	0x1000
#define D3DFVF_LASTBETA_D3DCOLOR	0x8000

// Vertex texture sampler slots (used by CTexture)
#define D3DVERTEXTEXTURESAMPLER0	(SAMPLER0 + 3)

// Primitive types
#define D3DPT_POINTLIST		1
#define D3DPT_LINELIST		2
#define D3DPT_LINESTRIP		3
#define D3DPT_TRIANGLELIST	4
#define D3DPT_TRIANGLESTRIP	5
#define D3DPT_TRIANGLEFAN	6

// Resource pool
#define D3DPOOL_DEFAULT		0
#define D3DPOOL_MANAGED		1
#define D3DPOOL_SYSTEMMEM	2
#define D3DPOOL_SCRATCH		3

// Resource types
#define D3DRTYPE_SURFACE		1
#define D3DRTYPE_VOLUME			2
#define D3DRTYPE_TEXTURE		3
#define D3DRTYPE_VOLUMETEXTURE	4
#define D3DRTYPE_CUBETEXTURE	5
#define D3DRTYPE_VERTEXBUFFER	6
#define D3DRTYPE_INDEXBUFFER	7

// Formats
typedef u32 D3DFORMAT;
#define D3DFMT_UNKNOWN		0
#define D3DFMT_INDEX16		101
#define D3DFMT_INDEX32		102
#define D3DFMT_A8R8G8B8		21
#define D3DFMT_X8R8G8B8		22
#define D3DFMT_A2R10G10B10	23
#define D3DFMT_R5G6B5		24
#define D3DFMT_A1R5G5B5		25
#define D3DFMT_X1R5G5B5		26
#define D3DFMT_R8G8B8		29
#define D3DFMT_DXT1			0x31545844
#define D3DFMT_DXT2			0x32545844
#define D3DFMT_DXT3			0x33545844
#define D3DFMT_DXT4			0x34545844
#define D3DFMT_DXT5			0x35545844

// Usage
#define D3DUSAGE_SOFTWAREPROCESSING	0x20
#define D3DUSAGE_WRITEONLY			0x08
#define D3DUSAGE_QUERY_FILTER		0x4000

// Lock flags
#define D3DLOCK_READONLY		0x10
#define D3DLOCK_DISCARD			0x2000
#define D3DLOCK_NOOVERWRITE		0x1000

// Driver caps constants used by visual code
#define D3DCAPS_READ_SCANLINE 0x00020000L
#define MAX_FVF_DECL_SIZE		(MAXD3DDECLLENGTH + 1)
#define MAXD3DDECLLENGTH		64

// Textures & sampler constants (from d3d9.h) - values are mirror of D3D9
#define SAMPLER0				0
#define D3D_COMMONSHADER_SAMPLER_SLOT_COUNT	16

// Culling
#define D3DCULL_NONE 1
#define D3DCULL_CW 2
#define D3DCULL_CCW 3

// Color helpers
#define D3DCOLOR_XRGB(r,g,b)	((u32)((((u32)r)<<16)|(((u32)g)<<8)|((u32)b)))
#define D3DCOLOR_RGBA(r,g,b,a)	D3DCOLOR_XRGB(r,g,b)

// Render state items (mirror D3DRS_* used by shader system)
enum {
	D3DRS_ZENABLE			= 7,
	D3DRS_FILLMODE			= 8,
	D3DRS_ZWRITEENABLE		= 14,
	D3DRS_ALPHATESTENABLE	= 15,
	D3DRS_ALPHAREF			= 24,
	D3DRS_ALPHABLENDENABLE	= 27,
	D3DRS_SRCBLEND			= 19,
	D3DRS_DESTBLEND			= 20,
	D3DRS_CULLMODE			= 22,
	D3DRS_SCISSORTESTENABLE	= 174,
	D3DRS_STENCILENABLE		= 52,
	D3DRS_STENCILMODE		= 53,
	D3DRS_COLORWRITEENABLE	= 168,
	D3DRS_SHADEMODE			= 33,
	D3DRS_LIGHTING			= 137,
	D3DRS_SPECULARENABLE	= 29,
};

// ----------------------------------------------------------------------------
// Buffer classes
// ----------------------------------------------------------------------------
typedef u32 D3DPOOL;
#define D3DPOOL_DEFAULT		0
#define D3DPOOL_MANAGED		1
#define D3DPOOL_SYSTEMMEM	2

struct D3DINDEXBUFFER_DESC
{
	UINT Size;
	UINT Usage;
	D3DPOOL Pool;
};
struct D3DVERTEXBUFFER_DESC
{
	UINT Size;
	UINT Usage;
	D3DPOOL Pool;
};

class ID3DVertexBuffer
{
public:
	u32						dwReference;
	u32						vCount;
	u32						vStride;
	std::vector<unsigned char> data;	// system-memory copy (used for wallmarks, pick, later upload)

	ID3DVertexBuffer() : dwReference(1), vCount(0), vStride(0) {}
	ULONG AddRef()  { return ++dwReference; }
	ULONG Release() { u32 r = --dwReference; if (0 == r) delete this; return r; }

	// D3D9-style Lock/Unlock over the CPU-side buffer (no GPU round trip needed)
	HRESULT Lock(u32 OffsetToLock, u32 SizeToLock, void** ppbData, u32 Flags)
	{
		(void)Flags;
		if (data.empty())
		{
			*ppbData = nullptr;
			return S_OK;
		}
		*ppbData = &data[std::min<size_t>(OffsetToLock, data.size())];
		return S_OK;
	}
	HRESULT Unlock() { return S_OK; }
	HRESULT GetDesc(D3DVERTEXBUFFER_DESC* pDesc) const
	{
		pDesc->Size = (UINT)data.size();
		pDesc->Pool = D3DPOOL_SYSTEMMEM;
		return S_OK;
	}
};

class ID3DIndexBuffer
{
public:
	u32						dwReference;
	u32						iCount;
	std::vector<unsigned char> data;

	ID3DIndexBuffer() : dwReference(1), iCount(0) {}
	ULONG AddRef()  { return ++dwReference; }
	ULONG Release() { u32 r = --dwReference; if (0 == r) delete this; return r; }

	HRESULT Lock(u32 OffsetToLock, u32 SizeToLock, void** ppbData, u32 Flags)
	{
		(void)Flags;
		if (data.empty())
		{
			*ppbData = nullptr;
			return S_OK;
		}
		*ppbData = &data[std::min<size_t>(OffsetToLock, data.size())];
		return S_OK;
	}
	HRESULT Unlock() { return S_OK; }
	HRESULT GetDesc(D3DINDEXBUFFER_DESC* pDesc) const
	{
		pDesc->Size = (UINT)data.size();
		pDesc->Pool = D3DPOOL_SYSTEMMEM;
		return S_OK;
	}
};

// ----------------------------------------------------------------------------
// Placeholder device object types (kept to satisfy the original signatures)
// ----------------------------------------------------------------------------
namespace xrRenderBGFX {
	// typedefs to keep the ORIGINAL xrRender headers compiling untouched
}

typedef struct ID3DVertexDeclaration {
	u32 dummy;
	ULONG AddRef()  { return 1; }
	ULONG Release() { return 0; }
} ID3DVertexDeclaration;	// dx11 name
typedef ID3DVertexDeclaration IDirect3DVertexDeclaration9;
typedef ID3DVertexDeclaration ID3DInputLayout;

class IDirect3DStateBlock9 { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
class ID3DState { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} }; // placeholder; SH_Atomic.h uses ID3DState*

// Shader objects (stubs)
class ID3DVertexShader { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
class ID3DPixelShader  { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
typedef ID3DVertexShader IDirect3DVertexShader9;
typedef ID3DPixelShader  IDirect3DPixelShader9;
class ID3DBlob { public: void* buffer; u32 size; };

class IDirect3DSurface9      { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };	// texture surface
class IDirect3DTexture9      { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
class IDirect3DVolumeTexture9{ public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
class IDirect3DBaseTexture9  { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
class ID3DRenderTargetView   { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
class ID3DDepthStencilView   { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };
class ID3DQuery              { public: u32 dummy; ULONG AddRef(){return 1;} ULONG Release(){return 0;} };

class ID3DXBuffer : public ID3DBlob { public: u32 dummy; };

struct D3D_TEXTURE2D_DESC { u32 Width; u32 Height; };	// Width/Height read by sh_texture.h
struct D3D_SURFACE_DESC    { u32 dummy; };
struct D3DVIEWPORT9        { u32 dummy; };
struct D3DGAMMARAMP        { u16 red[256]; u16 green[256]; u16 blue[256]; };

// ----------------------------------------------------------------------------
// Helpers to construct a DX9-style declaration from FVF
// ----------------------------------------------------------------------------
void bgfxD3DFVFTriangleListDeclaratorFromFVF(u32 fvf, D3DVERTEXELEMENT9* dcl);
u32  bgfxD3DFVFVertexSize(u32 fvf);

#define D3DXDeclaratorFromFVF(fvf, dcl)  ::bgfxD3DFVFTriangleListDeclaratorFromFVF((fvf),(dcl))
#define D3DXGetFVFVertexSize(fvf)        ::bgfxD3DFVFVertexSize((fvf))

// Helper macro-compat created in xrD3DDefs.h
// (_RELEASE is already defined by xrEngine/defines.h)

#endif // bgfxVisualTypes_included