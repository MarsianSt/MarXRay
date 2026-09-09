#ifndef	xrD3DDefs_included
#define	xrD3DDefs_included
#pragma once

// BGFX port: replace the original xrD3DDefs.h (which picked D3D9/DX11 types)
// with lightweight CPU-side buffer holder classes + D3D-compatible constants.

#include "bgfxVisualTypes.h"

// The aliases below match names that real d3d11.h also uses. If that header
// was already pulled in (e.g. by non-port TUs), keep its definitions instead.
#ifndef __d3d11_h__

// ---- ID3D vertex/pixel shader stubs (needed by sh_atomic) ----
typedef ID3DVertexShader	IDirect3DVertexShader9;
typedef ID3DPixelShader		IDirect3DPixelShader9;
typedef ID3DBlob			ID3DBlob;

// ---- texture object aliases ----
typedef IDirect3DTexture9			ID3DTexture2D;
typedef IDirect3DVolumeTexture9		ID3DTexture3D;
typedef IDirect3DBaseTexture9		ID3DBaseTexture;

// ---- other convenience typedefs ----
typedef ID3DVertexBuffer	ID3DVertexBuffer;
typedef ID3DIndexBuffer		ID3DIndexBuffer;

#endif // !__d3d11_h__

#endif	//	xrD3DDefs_included