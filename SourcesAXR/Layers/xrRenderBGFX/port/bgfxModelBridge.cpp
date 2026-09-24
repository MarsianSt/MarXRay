#include "stdafx.h"
#pragma hdrstop

#include "bgfxModelBridge.h"
#include "bgfxRenderCompat.h"
#include "FBasicVisual.h"

#include "../../../xrEngine/Render.h"
#include "../bgfx_capi.h"

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

	for (unsigned int i = 0; i < count; ++i)
	{
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
			// Visuals array. They submit into the sky view (kView=2).
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