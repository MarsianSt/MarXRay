#include "stdafx.h"
#include "bgfxVisualCompat.h"
#include "bgfxGpuCompat.h"

bgfxDeviceCompat g_BGfxDevice;
CHW HW;

struct CHWHook
{
	CHWHook() { HW.pDevice = &g_BGfxDevice; }
};
static CHWHook g_chwHook;

// ----------------------------------------------------------------------------
// FVF -> declaration / size helpers (mirror D3DX helpers)
// ----------------------------------------------------------------------------
void bgfxD3DFVFTriangleListDeclaratorFromFVF(u32 fvf, D3DVERTEXELEMENT9* dcl)
{
	u32 offset = 0;
	u32 idx = 0;
	D3DVERTEXELEMENT9* e = dcl;

	if (fvf & D3DFVF_XYZ)
	{
		e->Stream = 0; e->Offset = offset; e->Type = D3DDECLTYPE_FLOAT3; e->Method = 0; e->Usage = D3DDECLUSAGE_POSITION; e->UsageIndex = 0;
		offset += 12; e++;
	}
	else if (fvf & D3DFVF_XYZRHW)
	{
		e->Stream = 0; e->Offset = offset; e->Type = D3DDECLTYPE_FLOAT4; e->Method = 0; e->Usage = D3DDECLUSAGE_POSITIONT; e->UsageIndex = 0;
		offset += 16; e++;
	}
	else if (fvf & D3DFVF_XYZW)
	{
		e->Stream = 0; e->Offset = offset; e->Type = D3DDECLTYPE_FLOAT4; e->Method = 0; e->Usage = D3DDECLUSAGE_POSITION; e->UsageIndex = 0;
		offset += 16; e++;
	}

	if (fvf & D3DFVF_NORMAL)
	{
		e->Stream = 0; e->Offset = offset; e->Type = D3DDECLTYPE_FLOAT3; e->Method = 0; e->Usage = D3DDECLUSAGE_NORMAL; e->UsageIndex = 0;
		offset += 12; e++;
	}

	if (fvf & D3DFVF_DIFFUSE)
	{
		e->Stream = 0; e->Offset = offset; e->Type = D3DDECLTYPE_D3DCOLOR; e->Method = 0; e->Usage = D3DDECLUSAGE_COLOR; e->UsageIndex = 0;
		offset += 4; e++;
	}

	if (fvf & D3DFVF_SPECULAR)
	{
		e->Stream = 0; e->Offset = offset; e->Type = D3DDECLTYPE_D3DCOLOR; e->Method = 0; e->Usage = D3DDECLUSAGE_COLOR; e->UsageIndex = 1;
		offset += 4; e++;
	}

	u32 tex = (fvf & D3DFVF_TEX8) >> 8;	// number of texture coord sets
	for (u32 t = 0; t < tex; ++t)
	{
		e->Stream = 0; e->Offset = offset; e->Type = D3DDECLTYPE_FLOAT2; e->Method = 0; e->Usage = D3DDECLUSAGE_TEXCOORD; e->UsageIndex = (BYTE)t;
		offset += 8; e++;
	}

	// terminator (D3DDECL_END)
	e->Stream = 0xFF; e->Offset = 0xFF; e->Type = D3DDECLTYPE_UNUSED; e->Method = 0; e->Usage = 0; e->UsageIndex = 0;
}

u32 bgfxD3DFVFVertexSize(u32 fvf)
{
	u32 sz = 0;
	if (fvf & D3DFVF_XYZ) sz += 12;
	else if (fvf & (D3DFVF_XYZB1|D3DFVF_XYZB2|D3DFVF_XYZB3|D3DFVF_XYZB4|D3DFVF_XYZB5)) sz += 16;
	else if (fvf & D3DFVF_XYZRHW) sz += 16;
	else if (fvf & D3DFVF_XYZW) sz += 16;
	else sz += 12;

	if (fvf & D3DFVF_NORMAL) sz += 12;
	if (fvf & D3DFVF_PSIZE) sz += 4;
	if (fvf & D3DFVF_DIFFUSE) sz += 4;
	if (fvf & D3DFVF_SPECULAR) sz += 4;

	u32 tex = (fvf & D3DFVF_TEX8) >> 8;
	sz += tex * 8;

	bool bLastBETA_UBYTE4 = (fvf & D3DFVF_LASTBETA_UBYTE4) != 0;
	u32 numBlendWeights = (fvf & D3DFVF_XYZB5) >> 1;
	if (numBlendWeights && bLastBETA_UBYTE4) numBlendWeights -= 1;
	sz += (numBlendWeights & 0x7) * 4;	// blend weights (floats)

	return sz;
}

// ----------------------------------------------------------------------------
// Buffer creation
// ----------------------------------------------------------------------------
ID3DVertexBuffer* bgfxCreateVertexBuffer(const void* data, u32 count, u32 stride)
{
	ID3DVertexBuffer* b = xr_new<ID3DVertexBuffer>();
	b->vCount = count;
	b->vStride = stride;
	b->data.resize(size_t(count) * stride);
	if (data && b->data.size())
		memcpy(&b->data[0], data, b->data.size());
	return b;
}

ID3DVertexBuffer* bgfxCreateVertexBufferEmpty(u32 count, u32 stride)
{
	return bgfxCreateVertexBuffer(NULL, count, stride);
}

ID3DIndexBuffer* bgfxCreateIndexBuffer(const void* data, u32 count)
{
	ID3DIndexBuffer* b = xr_new<ID3DIndexBuffer>();
	b->iCount = count;
	b->data.resize(size_t(count) * sizeof(u16));
	if (data && b->data.size())
		memcpy(&b->data[0], data, b->data.size());
	return b;
}

ID3DIndexBuffer* bgfxCreateIndexBufferEmpty(u32 count)
{
	return bgfxCreateIndexBuffer(NULL, count);
}

// D3D9-style create-then-Lock/CopyMemory buffers
ID3DVertexBuffer* bgfxCreateBufferBytes(u32 bytes)
{
	ID3DVertexBuffer* b = xr_new<ID3DVertexBuffer>();
	b->vCount = bytes / 4;	// guess - stride/count are set explicitly later as needed
	b->vStride = 4;
	b->data.resize(bytes);
	return b;
}

ID3DIndexBuffer* bgfxCreateIndexBufferBytes(u32 bytes)
{
	ID3DIndexBuffer* b = xr_new<ID3DIndexBuffer>();
	b->iCount = bytes / sizeof(u16);
	b->data.resize(bytes);
	return b;
}