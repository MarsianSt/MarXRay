#pragma once

#include "stdafx.h"
#include "../bgfx_capi.h"

// BGFX static wallmarks (bullet holes, blood, explosion marks).
//
// CPU-side port of Layers/xrRender/WallmarksEngine.cpp: the touched level
// faces are gathered through a CDB box query, clipped against a small 3D
// ortho frustum, triangulated and cached with a TTL. The cached quads are
// submitted into the world view (view 0) after the world pass with the
// reference multiply blend, depth test on and depth write off.
//
// Skeleton wallmarks (blood on animated meshes) are not implemented yet.

namespace bgfxWallMarks
{
	void AddWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
		bgfx_texture_handle_t texture, float sz);
	void Clear();
	void Render();
}
