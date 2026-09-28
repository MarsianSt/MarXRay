#pragma once

#include "stdafx.h"
#include "../bgfx_capi.h"

// BGFX static wallmarks (bullet holes, blood, explosion marks).
//
// CPU-side port of Layers/xrRender/WallmarksEngine.cpp: the touched level
// faces are gathered through a CDB box query, clipped against a small 3D
// ortho frustum, triangulated and cached with a TTL. The cached quads are
// submitted into their own view (bgfxHDR::kWallmarkView), which sits after the
// scene and scene FX views and before the SSAO view, and multiply into the
// albedo G-buffer alone with depth test on and depth write off - the AXR
// phase_wallmarks (r4_rendertarget_phase_combine.cpp:791-804, called from
// r4_R_render.cpp:464). They therefore take the deferred lighting and the
// height fog of the surface they lie on, exactly like the reference.
//
// Skeleton wallmarks (blood on animated meshes) are not implemented yet.

namespace bgfxWallMarks
{
	void AddWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
		bgfx_texture_handle_t texture, float sz);
	void Clear();
	void Render();
}
