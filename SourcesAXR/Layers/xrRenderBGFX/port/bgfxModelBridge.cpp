#include "stdafx.h"
#pragma hdrstop

#include "bgfxModelBridge.h"
#include "bgfxRenderCompat.h"
#include "FBasicVisual.h"

#include "../../../xrEngine/Render.h"
#include "../../../xrEngine/device.h"
#include "../bgfx_capi.h"

#include <algorithm>

// ============================================================================
// Dynamic-visual submission hook.
//
// The header deliberately keeps the CPU-side accumulator type-agnostic; here we
// cast to dxRender_Visual* and dispatch by Type. Skeleton/skinning submission is
// not yet ported (no dynamic VB writer, no per-bone matrix buffer, no skinned
// program), so this pass currently:
//   * validates the pointers,
//   * buckets by visual type for diagnostics,
//   * (TODO) hands skinned meshes to a skinning submission path once the
//     TEnumBoneVertices -> dynamic-VB pipeline exists.
//
// It is intentionally safe to call every frame with arbitrary counts.
//
// Order. AXR walks the dynamic renderables front-to-back: render_main fills
// lstRenderablesMain with g_SpatialSpace->q_frustum(..., O_ORDERED, ...)
// (r4_R_render.cpp:59-65) and then std::sort's it with pred_sp_sort
// (r4_R_render.cpp:68), whose predicate is `d1 < d2` on
// spatial.sphere.P.distance_to_sqr(Device.vCameraPosition)
// (r4_R_render.cpp:9-14) - the nearest object first. The walk then calls
// renderable_Render() in that order, so every visual it produces enters the
// graph in front-to-back order, and the graph keeps that order for everything
// that is not re-sorted itself: add_leafs_Dynamic's default branch feeds
// r_dsgraph_insert_dynamic (r__dsgraph_build.cpp:793-801), and a particle
// material's `: sorting (3, false)` clears bStrictB2F
// (dx10ResourceManager_Scripting.cpp:93 -> CBlender_Compile::SetParams,
// Blender_Recorder.cpp:147-155, Shader.cpp:71), so the effect lands in
// mapMatrixPasses (r__dsgraph_build.cpp:203-209) instead of mapSorted and is
// drawn in insertion order by r_dsgraph_render_graph
// (r__dsgraph_render.cpp:397-449, whose std::sort only permutes the
// VS/PS/state/constant buckets by SSA, never the items inside one bucket,
// r__dsgraph_build.cpp:248). Sorting the accumulator by the same key therefore
// reproduces the reference's draw order exactly - which is why this is the only
// distance sort the particle path needs.
//
// The key is the renderable's spatial sphere centre, i.e. the object position,
// not the visual's transformed bounding sphere. The accumulator's world matrix
// translation (Fmatrix.m.c, row 3) is that same point: CParticlesObject hands
// add_Visual its object transform (CParticlesObject::renderable_Render ->
// set_Transform), and its ROS centre is derived from it.
//
// HUD visuals are not part of lstRenderablesMain - the reference collects them
// through g_hud->Render_Last (bgfxRenderCompat.cpp, bgfxRenderHudPass) - so they
// keep their collection order and are emitted after the world list, as the
// separate HUD pass does.
//
// Ties: the reference uses std::sort, so its order for equal distances is
// unspecified. stable_sort keeps the query order for those, which is the one
// observable choice that cannot drift from frame to frame.
// ============================================================================
extern "C" void bgfxSubmitSkinnedFrame(const bgfxDynamicVisualEntry* entries,
					unsigned int count)
{
	if (!entries || count == 0)
		return;

	// Cap the diag counters; we only want one snapshot per session, not spam.
	static unsigned int s_frameIdx  = 0;
	static bool         s_logged    = false;
	++s_frameIdx;

	unsigned int byType[32] = {};
	unsigned int drawn      = 0;
	unsigned int skipped    = 0;
	unsigned int hudSeen    = 0;

	// The walk order the reference's std::sort(lstRenderablesMain, pred_sp_sort)
	// produces: world entries front-to-back, HUD entries after them untouched.
	static thread_local std::vector<unsigned int> s_order;
	s_order.clear();
	s_order.reserve(count);
	for (unsigned int i = 0; i < count; ++i)
		if (!entries[i].hud)
			s_order.push_back(i);
	if (s_order.size() > 1)
	{
		const Fvector camPos = Device.vCameraPosition;
		std::stable_sort(s_order.begin(), s_order.end(),
			[&entries, &camPos](unsigned int a, unsigned int b)
			{
				// pred_sp_sort, r4_R_render.cpp:9-14 - squared distance from the
				// camera to the object position (the renderable's spatial sphere
				// centre), nearest first.
				const float* ma = entries[a].transform;
				const float* mb = entries[b].transform;
				const float ax = ma[12] - camPos.x, ay = ma[13] - camPos.y, az = ma[14] - camPos.z;
				const float bx = mb[12] - camPos.x, by = mb[13] - camPos.y, bz = mb[14] - camPos.z;
				return (ax*ax + ay*ay + az*az) < (bx*bx + by*by + bz*bz);
			});
	}
	for (unsigned int i = 0; i < count; ++i)
		if (entries[i].hud)
			s_order.push_back(i);

	for (unsigned int order = 0; order < count; ++order)
	{
		const unsigned int i = s_order[order];
		const bgfxDynamicVisualEntry& e = entries[i];
		if (!e.visual)
		{
			++skipped;
			continue;
		}
		if (e.hud)
			++hudSeen;

		dxRender_Visual* v = static_cast<dxRender_Visual*>(e.visual);
		const unsigned int t = v->Type;
		if (t < 32)
			++byType[t];

		switch (t)
		{
		case MT_NORMAL:			// Fvisual — rigid, static VB/IB
		case MT_SKELETON_ANIM:		// FSkeletonAnim
		case MT_SKELETON_RIGID:		// FSkeletonRigid
			// Real skin handoff: the surviving visual + its transform go to
			// the port submit hook (same world prog/layout the world pass
			// submits with). The hook owns the VB/IB/transform upload and the
			// bgfx_submit; it returns false when it cannot bind yet.
			if (bgfxSubmitSkinnedVisual(e.visual, (const float*)e.transform, e.hud))
				++drawn;
			break;

		case MT_PARTICLE_EFFECT:	// CParticleEffect
		case MT_PARTICLE_GROUP:		// CParticleGroup
			// Game particle objects (CParticlesObject::renderable_Render ->
			// add_Visual) arrive through this accumulator, not the level's
			// Visuals array. They submit into the HDR scene FX view (kView=6).
			bgfxDrawParticleVisual(v);
			++drawn;
			break;

		default:
			// Progressive/hierarchy: handled by the world pass, not by the
			// dynamic accumulator. Ignore here.
			++skipped;
			break;
		}
	}

	// One-shot snapshot once we've actually seen a few frames of content.
	if (!s_logged && s_frameIdx >= 4 && (drawn || skipped))
	{
		LogInfo("--- bgfxport DYN: n=%u drawn=%u skipped=%u hud=%u"
			" t0=%u t1=%u t2=%u t3=%u t4=%u t5=%u t6=%u t10=%u t24=%u",
			count, drawn, skipped, hudSeen,
			byType[0],  byType[1],  byType[2],  byType[3],
			byType[4],  byType[5],  byType[6],  byType[10], byType[24]);
		s_logged = true;
	}
}

// ============================================================================
// Non-inline companion for callers that cannot include the inline header
// (e.g. the non-PCH bgfxRenderInterface front-end). Keeps the bridge a single
// TU for the C++ side while the inline stubs stay header-only for the game.
// ============================================================================
extern "C" void* bgfxModelCreateParticles(const char* name)
{
	return RImplementation.model_CreateParticles(name);
}

extern "C" void bgfxBridgeRenderDynamic()
{
	bgfxRenderDynamic();
}

extern "C" void bgfxBridgeClearDynamic()
{
	bgfxClearDynamic();
}

extern "C" void bgfxBridgeAddDynamic(void* V, const float* m, int hud, int invisible)
{
	bgfxAddDynamicVisual(V, m, hud, invisible);
}