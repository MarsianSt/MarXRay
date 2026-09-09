#pragma once

#include "stdafx.h"
#include "FBasicVisual.h"
#include "../../../Include/xrRender/ParticleCustom.h"
#include "../../../xrEngine/Fmesh.h"

// ---------------------------------------------------------------------------
// Minimal particle-system visual for the BGFX port.
//
// The real CParticleEffect / CParticleGroup (Layers/xrRender) are not ported
// yet. This stub implements the IParticleCustom surface the game relies on
// (ParticlesObject.cpp) so particle systems can be created, updated and
// replayed without crashing. It is stateless and renders nothing.
// ---------------------------------------------------------------------------
class bgfxStubParticleCustom : public dxRender_Visual, public IParticleCustom
{
public:
	bgfxStubParticleCustom()
	{
		Type = MT_PARTICLE_EFFECT;
	}

	// dxRender_Visual (rest of the defaults are safe no-ops)
	virtual void Load(const char* N, IReader* data, u32 dwFlags) override
	{
		(void)N; (void)data; (void)dwFlags;
	}
	virtual void Release() override {}	// destroyed via model_Delete/xr_delete, not Release
	virtual void Copy(dxRender_Visual* from) override { (void)from; }
	virtual void Spawn() override {}

	// IRenderVisual
	virtual IParticleCustom* dcast_ParticleCustom() override { return this; }

	// IParticleCustom
	virtual void OnDeviceCreate() override {}
	virtual void OnDeviceDestroy() override {}
	virtual void UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM) override
	{
		(void)m; (void)velocity; (void)bXFORM;
	}
	virtual void OnFrame(u32 dt) override { (void)dt; }
	virtual void Play() override {}
	virtual void Stop(BOOL bDefferedStop = TRUE) override { (void)bDefferedStop; }
	virtual BOOL IsPlaying() override { return FALSE; }
	virtual u32 ParticlesCount() override { return 0; }
	virtual float GetTimeLimit() override { return 0.f; }
	virtual const shared_str Name() override { return m_name; }
	virtual void SetHudMode(BOOL b) override { (void)b; m_hud = FALSE; }
	virtual BOOL GetHudMode() override { return m_hud; }

private:
	shared_str m_name = "stub_particles";
	BOOL m_hud = FALSE;
};

// Per-instance object: every particle system gets its own stub (like the real
// dx9 PSVisual). The engine calls model_Delete() on it with xr_delete(), so it
// must be a normal heap object — a shared static here caused a bogus free()
// (heap corruption / silent 0xC0000374) on the first particle expiry.
inline IRenderVisual* bgfxStubParticleCreate()
{
	return xr_new<bgfxStubParticleCustom>();
}