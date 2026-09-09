#ifndef bgfxVBMacros_included
#define bgfxVBMacros_included
#pragma once

// ============================================================================
// BGFX port: make the D3D device-call sites in the ported visual code compile
// and function against our CPU-side buffer holders.
//
//  - HW.pDevice->CreateVertexBuffer / CreateIndexBuffer  ->  bgfx* helpers
//  - HW.pDevice->CreateVertexDeclaration / CreateInputLayout -> ignored
//  - vb/ib->Lock/Unlock  ->  no-op (buffers hold CPU-side copies already)
//  - HW.stats_manager.* -> ignored
//  - dx10BufferUtils::Create*Buffer -> bgfx* helpers
// ============================================================================

#include "bgfxVisualCompat.h"

#define BGFX_CREATE_VB(pp, data, bytes, stride, cnt)	(*(pp) = bgfxCreateVertexBuffer((data),(cnt),(stride)))
#define BGFX_CREATE_VB_EMPTY(pp, bytes, stride, cnt)	(*(pp) = bgfxCreateVertexBufferEmpty((cnt),(stride)))
#define BGFX_CREATE_IB(pp, data, bytes, cnt)			(*(pp) = bgfxCreateIndexBuffer((data),(cnt)))
#define BGFX_CREATE_IB_EMPTY(pp, bytes, cnt)			(*(pp) = bgfxCreateIndexBufferEmpty((cnt)))

#define BGFX_LOCK_VB(p, sub, flags, pp)		((*pp) = (BYTE*)p->data.empty()?0:&p->data[0])
#define BGFX_LOCK_IB(p, sub, pp)			((*pp) = (BYTE*)p->data.empty()?0:&p->data[0])
#define BGFX_UNLOCK(p)						((void)0)

// dx11 backend paths (kept self-contained so FVisual/FSkinned compile as-is)
inline void bgfxCompatCreateVertexBuffer(void** pp, const void* data, size_t bytes, u32 stride, u32 cnt)
{
	*pp = bgfxCreateVertexBuffer(data, cnt, stride);
}
inline void bgfxCompatCreateIndexBuffer(void** pp, const void* data, size_t bytes, u32 cnt)
{
	*pp = bgfxCreateIndexBuffer(data, cnt);
}

#endif // bgfxVBMacros_included