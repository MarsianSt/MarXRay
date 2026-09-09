#ifndef bgfxGpuCompat_included
#define bgfxGpuCompat_included
#pragma once

#include "bgfxVisualTypes.h"
#include "bgfxVisualCompat.h"

// ============================================================================
// BGFX port: minimal stand-ins for the global render-hardware object `HW` and
// its members that the ported visual code still references. All GPU work is
// stubbed - buffers live on the CPU and are uploaded to bgfx later.
// ============================================================================

// stats_manager: no-op counters, API-compatible with the D3D one.
struct bgfxStatsManager
{
	void increment_stats_vb(void*)   {}
	void decrement_stats_vb(void*)   {}
	void increment_stats_ib(void*)   {}
	void decrement_stats_ib(void*)   {}
	void increment_stats_rtarget(void*) {}
	void decrement_stats_rtarget(void*) {}
  	void increment_stats_vdecl(void*)  {}
  	void decrement_stats_vdecl(void*)  {}
  	void increment_stats_smap(void*)   {}
  	void decrement_stats_smap(void*)   {}
};

struct bgfxGeomCaps
{
	bool  bSoftware;
	u32   dwRegisters;
	u32   dwHWLevel;
	bgfxGeomCaps() : bSoftware(false), dwRegisters(256), dwHWLevel(9) {}
};

struct bgfxRasterCaps
{
	bool  bNonPow2;
	bgfxRasterCaps() : bNonPow2(true) {}
};

// CPU-only "device" facade exposing just the Create*Buffer entry points.
struct bgfxDeviceCompat
{
	HRESULT CreateVertexBuffer(u32 size, u32 /*usage*/, u32 /*fvf*/, u32 /*pool*/,
	                           ID3DVertexBuffer** ppvb, void* /*pSharedHandle*/)
	{
		*ppvb = bgfxCreateBufferBytes(size);
		return S_OK;
	}
	HRESULT CreateIndexBuffer(u32 size, u32 /*usage*/, u32 /*format*/, u32 /*pool*/,
	                          ID3DIndexBuffer** ppib, void* /*pSharedHandle*/)
	{
		*ppib = bgfxCreateIndexBufferBytes(size);
		return S_OK;
	}
	HRESULT SetTexture(u32 /*stage*/, const void* /*tex*/) { return S_OK; }
};

struct CHW
{
	bgfxDeviceCompat* pDevice;
	bgfxStatsManager  stats_manager;
	struct {
		bgfxGeomCaps   geometry;
		bgfxRasterCaps raster;
	} Caps;
	void*             pD3D;
	u32               DevAdapter;
	u32               DevT;
	D3DFORMAT         fTarget;

	CHW() : pDevice(0), pD3D(0), DevAdapter(0), DevT(0), fTarget(D3DFMT_UNKNOWN) {}
};

extern CHW HW;
extern bgfxDeviceCompat g_BGfxDevice;

#endif // bgfxGpuCompat_included