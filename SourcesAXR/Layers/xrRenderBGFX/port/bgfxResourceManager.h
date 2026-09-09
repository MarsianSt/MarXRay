#ifndef bgfxResourceManager_included
#define bgfxResourceManager_included
#pragma once

// ============================================================================
// BGFX port: light-weight resource manager used through the DEV macro.
// The original dxRenderDeviceRender::Resources (CResourceManager) compiled
// HLSL shaders, created render targets, etc. In the BGFX port none of that
// happens - we only need reference-lifecycle management for the small set of
// shader/geometry objects the copied visual classes create at load time.
//
// Every method in the original public surface is provided as a no-op so the
// ported files compile unchanged.
// ============================================================================

#include "Shader.h"

class bgfxResourceManagerLite
{
public:
	// ---- geometry ----
	SGeometry*		CreateGeom	(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib);
	SGeometry*		CreateGeom	(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib);
	void			DeleteGeom	(SGeometry* g);

	// ---- shader ----
	Shader*			Create		(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices);
	Shader*			Create		(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices);
	void			Delete		(Shader* s);

	// ---- list objects ----
	void			_DeleteTextureList	(class STextureList* p);
	void			_DeleteMatrixList	(class SMatrixList* p);
	void			_DeleteConstantList	(class SConstantList* p);
	void			_DeletePass			(class SPass* p);
	void			_DeleteElement		(class ShaderElement* p);
	void			_DeleteDecl			(class SDeclaration* p);
};

// Returns the global VERBOSE resource manager bucket (used by DEV macro).
bgfxResourceManagerLite* bgfxResourceManager();

#endif // bgfxResourceManager_included