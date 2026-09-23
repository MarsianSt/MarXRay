#ifndef bgfxRenderCompat_included
#define bgfxRenderCompat_included
#pragma once

// ============================================================================
// BGFX port: CRender compat layer (the global `::RImplementation`).
// The ported xrRender visual classes reference `::RImplementation` for
// geometry/shader retrieval and model management. Rendering itself is stubbed.
//
// CModelPool (ModelPool.cpp) owns the actual visual lifecycle; CRender exposes
// a thin set of helpers routed to it / to the CPU-side buffer store.
// ============================================================================

#include "../../../xrEngine/vis_common.h"
#include "../../../xrCore/intrusive_ptr.h"
#include "Shader.h"

#include <vector>
class IReader;
class dxRender_Visual;
class IRenderVisual;
class CModelPool;
struct FSlideWindowItem;
class CSkeletonWallmark;

class CRender
{
public:
	// concrete renderer phases used by the ported visuals
	enum ERenderPhase
	{
		PHASE_NONE = 0,
		PHASE_NORMAL,
		PHASE_SMAP,
	};

	CRender();
	~CRender();

	u32			phase;				// written by FVisual::Render
	CModelPool*	pool;				// installed lazily on first model access
	xr_vector<IRenderVisual*>	Visuals;	// indexed visuals (game content)
	// ---- level geometry (visuals + hierarchical links) ----
	void				load_visuals	(IReader* fs);
	void				load_buffers	(IReader* geomFile, bool fast);
	void				load_swis		(IReader* geomFile);

	// ---- geometry retrieval (returns CPU-side, reference-counted buffers) ----
	class ID3DVertexBuffer* getVB			(int ID, bool fast = false);
	class ID3DIndexBuffer*  getIB			(int ID, bool fast = false);
	struct D3DVERTEXELEMENT9* getVB_Format	(int ID, bool fast = false);
	// ---- shader / misc ----
	ref_shader	getShader	(int id);	// shader pipeline stubbed -> empty
	void		add_SkeletonWallmark(const intrusive_ptr<CSkeletonWallmark,intrusive_base>& wm) {}
	bool		occ_visible			(void*) { return true; }
	// ---- model pool bridge (implemented using CModelPool) ----
	IRenderVisual*	model_Create	(LPCSTR name, IReader* data = 0);
	IRenderVisual*	model_CreateParticles(LPCSTR name);
	IRenderVisual*	model_CreateChild(LPCSTR name, IReader* data = 0);
	IRenderVisual*	model_Duplicate	(IRenderVisual* V);
	void			model_Delete	(IRenderVisual*& V, BOOL bDiscard = FALSE);
	IRenderVisual*	getVisual		(int id);
	FSlideWindowItem* getSWI		(u32 ID);
};

// Global instances (defined in bgfxRenderCompat.cpp)
extern CRender		RImplementation;
extern CRender&		ERender;

// Installs the CModelPool on RImplementation (idempotent).
void	bgfxEnsureModelPool();

// Submits a particle visual (effect or group) into the particle view.
void	bgfxDrawParticleVisual(dxRender_Visual* v);

#endif // bgfxRenderCompat_included