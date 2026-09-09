#include "stdafx.h"
#pragma hdrstop

#include "bgfxResourceManager.h"

// ============================================================================
// BGFX resource manager - all operations are effectively no-ops because the
// rendering pipeline is stubbed. Objects are created minimal and leaked-free.
// ============================================================================

bgfxResourceManagerLite* bgfxResourceManager()
{
	static bgfxResourceManagerLite s_inst;
	return &s_inst;
}

SGeometry* bgfxResourceManagerLite::CreateGeom(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
	SGeometry* g = xr_new<SGeometry>();
	g->dcl = ref_declaration();
	g->vb  = vb;  if (vb) vb->AddRef();
	g->ib  = ib;  if (ib) ib->AddRef();
	g->vb_stride = FVF ? bgfxD3DFVFVertexSize(FVF) : 0;
	return g;
}

SGeometry* bgfxResourceManagerLite::CreateGeom(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
	SGeometry* g = xr_new<SGeometry>();
	g->dcl = ref_declaration();
	g->vb  = vb;  if (vb) vb->AddRef();
	g->ib  = ib;  if (ib) ib->AddRef();
	// derive stride from the (terminated) declaration, fall back to the VB's own stride
	g->vb_stride = 0;
	if (vb)
	{
		g->vb_stride = vb->vStride;
	}
	else if (decl)
	{
		u32 off = 0, maxOff = 0;
		for (u32 i = 0; decl[i].Stream != 0xFF && decl[i].Type != D3DDECLTYPE_UNUSED; ++i)
			maxOff = std::max(maxOff, (u32)(decl[i].Offset + 16));
		g->vb_stride = maxOff;
	}
	return g;
}

void bgfxResourceManagerLite::DeleteGeom(SGeometry* g)
{
	if (g)
	{
		_RELEASE(g->vb);
		_RELEASE(g->ib);
		g->dcl._set(NULL);
		xr_delete(g);
	}
}

Shader* bgfxResourceManagerLite::Create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	// geometry not loaded; empty shader object suffices
	return xr_new<Shader>();
}

Shader* bgfxResourceManagerLite::Create(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	return xr_new<Shader>();
}

void bgfxResourceManagerLite::Delete(Shader* s)
{
	xr_delete(s);
}

void bgfxResourceManagerLite::_DeleteTextureList(STextureList* p)	{ xr_delete(p); }
void bgfxResourceManagerLite::_DeleteMatrixList(SMatrixList* p)		{ xr_delete(p); }
void bgfxResourceManagerLite::_DeleteConstantList(SConstantList* p)	{ xr_delete(p); }
void bgfxResourceManagerLite::_DeletePass(SPass* p)					{ xr_delete(p); }
void bgfxResourceManagerLite::_DeleteElement(ShaderElement* p)		{ xr_delete(p); }
void bgfxResourceManagerLite::_DeleteDecl(SDeclaration* p)			{ xr_delete(p); }