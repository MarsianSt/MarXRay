//----------------------------------------------------
// BGFX port: real particle-systems library (CPSLibrary).
// Ported from Layers/xrRender/PSLibrary.{h,cpp} without the editor surface.
//----------------------------------------------------
#ifndef PSLibraryH
#define PSLibraryH

#include "stdafx.h"
#include "../../../Include/xrRender/particles_systems_library_interface.hpp"

namespace PS {
	class CPEDef;
	class CPGDef;
	DEFINE_VECTOR(CPEDef*, PEDVec, PEDIt);
	DEFINE_VECTOR(CPGDef*, PGDVec, PGDIt);
}

class ECORE_API CPSLibrary : public particles_systems::library_interface
{
public:
	CPSLibrary() {}
	~CPSLibrary() {}

	bool				OnCreate();
	void				OnDestroy();
	bool				Load(LPCSTR nm);

	PS::PEDIt			FindPEDIt(LPCSTR name);
	PS::CPEDef*			FindPED(LPCSTR name);
	PS::PGDIt			FindPGDIt(LPCSTR name);
	PS::CPGDef*			FindPGD(LPCSTR name);

	virtual PS::CPGDef const* const*	particles_group_begin() const override;
	virtual PS::CPGDef const* const*	particles_group_end() const override;
	virtual void						particles_group_next(PS::CPGDef const* const*& iterator) const override;
	virtual shared_str const&			particles_group_id(PS::CPGDef const& particles_group) const override;

private:
	PS::PEDVec			m_PEDs;
	PS::PGDVec			m_PGDs;
};

// Global library, lazily loaded from $game_data$/particles.xr on first use.
CPSLibrary& bgfxPSLibrary();

// Same instance exposed through the library interface (for callers that must
// not include this header / the port stdafx).
particles_systems::library_interface const& bgfxPSLibraryInterface();

#define PS_VERSION				0x0001
#define PS_CHUNK_VERSION		0x0001
#define PS_CHUNK_FIRSTGEN		0x0002
#define PS_CHUNK_SECONDGEN		0x0003
#define PS_CHUNK_THIRDGEN		0x0004

#endif // PSLibraryH
