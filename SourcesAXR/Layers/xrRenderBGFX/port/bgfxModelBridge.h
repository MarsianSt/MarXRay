#pragma once

// ---------------------------------------------------------------------------
// Lightweight C bridge between the (non-PCH) bgfx render interface and the
// ported model layer. Avoids pulling heavy port headers (Shader.h,
// sh_atomic.h, ...) into non-port translation units that use PCH.
//
// Two independent concerns live here:
//   * inline CPU-side accumulator of dynamic visuals for one frame,
//   * extern "C" entry points consumed by bgfxRenderInterface.
// ---------------------------------------------------------------------------

#include <vector>
#include <cstring>

// ---------------------------------------------------------------------------
// Per-frame dynamic-visual entry: what the game's Calculate() hands us via
// set_Transform + add_Visual, before any GPU submission happens.
// ---------------------------------------------------------------------------
struct bgfxDynamicVisualEntry
{
	void*	visual		= nullptr;	// IRenderVisual (port dxRender_Visual*)
	float	transform[16]	= {};	// row-major world matrix, copied verbatim
	int	hud		= 0;
	int	invisible	= 0;	// set_Invisible flag; engine says "won't draw"
};

inline std::vector<bgfxDynamicVisualEntry>& bgfxDynamicStore()
{
	static std::vector<bgfxDynamicVisualEntry> s_store;
	return s_store;
}

inline void bgfxAddDynamicVisual(void* V, const float* m, int hud, int invisible)
{
	if (!V || !m)
		return;
	bgfxDynamicVisualEntry& e = bgfxDynamicStore().emplace_back();
	e.visual = V;
	std::memcpy(e.transform, m, sizeof(e.transform));
	e.hud        = hud;
	e.invisible  = invisible;
}

inline void bgfxClearDynamic()
{
	bgfxDynamicStore().clear();
}

// ---------------------------------------------------------------------------
// Submission hook, implemented in the port TU (bgfxModelBridge.cpp).
// Receives the compacted, visible-only slice for this frame. Returns nothing;
// ownership stays with the caller. Must not throw, must tolerate count == 0.
// ---------------------------------------------------------------------------
extern "C" void bgfxSubmitSkinnedFrame(const bgfxDynamicVisualEntry* entries,
					unsigned int count);

// Real skin-handoff hook: implemented in the compat layer (bgfxRenderCompat,
// where the world VBH/IBH/layout live), called by the CPU pass for each
// surviving dynamic visual. Returns true only when the visual actually reached
// a brazen submit this frame, so the bridge can report drawn vs skipped honestly.
extern "C" bool bgfxSubmitSkinnedVisual(const void* visual, const float* transform,
					int hud);

// HUD pass: drives g_hud->Render_Last() (collects hand/weapon visuals with
// hud=true) and submits them with the dedicated HUD view/projection.
extern "C" void bgfxRenderHudPass();

inline void bgfxRenderDynamic(int hudFilter = -1)
{
	auto& store = bgfxDynamicStore();
	if (store.empty())
		return;

	// Reuse a per-thread scratch so we don't allocate each frame. Kept in the
	// store's own TLS slot to avoid a global mutable (header is header-only).
	static thread_local std::vector<bgfxDynamicVisualEntry> s_visible;
	s_visible.clear();
	s_visible.reserve(store.size());

	for (const auto& e : store)
		if (!e.invisible && (hudFilter < 0 || e.hud == hudFilter))
			s_visible.push_back(e);

	if (!s_visible.empty())
		bgfxSubmitSkinnedFrame(s_visible.data(), (unsigned int)s_visible.size());
}

// ---------------------------------------------------------------------------
// Driven by ::Render->Calculate(); the engine layer performs the spatial
// traversal, we just expose the accumulated store. No-op for now.
// ---------------------------------------------------------------------------
inline void bgfxCalculateDynamic()
{
}

// ---------------------------------------------------------------------------
// extern "C" surface used by the non-PCH render interface
// ---------------------------------------------------------------------------
class IRenderVisual;
class IReader;

extern "C"
{
	void*	bgfxModelCreate		(const char* name);
	void*	bgfxModelCreateChild	(const char* name, IReader* data);
	void*	bgfxModelDuplicate	(void* V);
	void	bgfxModelDelete		(void** V, int bDiscard);
	void*	bgfxGetVisual		(int id);
	void	bgfxLoadVisuals		(IReader* fs);
	void	bgfxLoadGeometry	();
	void	bgfxRenderWorld		();
	void	bgfxRenderSceneObjects	();
	void	bgfxDumpLevelGeom	();
}