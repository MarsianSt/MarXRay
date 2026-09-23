//----------------------------------------------------
// BGFX port: real particle-systems library (CPSLibrary).
// Ported from Layers/xrRender/PSLibrary.cpp (binary particles.xr loader).
//----------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "PSLibrary.h"
#include "ParticleEffect.h"
#include "ParticleGroup.h"
#include "../../../xrEngine/x_ray.h"

#include <algorithm>

using namespace PS;

namespace
{
	bool ped_sort_pred(const CPEDef* a, const CPEDef* b) { return xr_strcmp(a->Name(), b->Name()) < 0; }
	bool pgd_sort_pred(const CPGDef* a, const CPGDef* b) { return xr_strcmp(a->m_Name, b->m_Name) < 0; }
	bool ped_find_pred(const CPEDef* a, LPCSTR b) { return xr_strcmp(a->Name(), b) < 0; }
	bool pgd_find_pred(const CPGDef* a, LPCSTR b) { return xr_strcmp(a->m_Name, b) < 0; }
}

bool CPSLibrary::OnCreate()
{
	string_path fn;
	if (!bWinterMode)
	{
		FS.update_path(fn, "$game_data$", "particles.xr");
		return Load(fn);
	}
	FS.update_path(fn, "$game_data$", "particles_winter.xr");
	return Load(fn);
}

void CPSLibrary::OnDestroy()
{
	for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
		(*e_it)->DestroyShader();

	for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
		xr_delete(*e_it);
	m_PEDs.clear();

	for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it)
		xr_delete(*g_it);
	m_PGDs.clear();
}

PS::PEDIt CPSLibrary::FindPEDIt(LPCSTR Name)
{
	if (!Name)
		return m_PEDs.end();
	PS::PEDIt I = std::lower_bound(m_PEDs.begin(), m_PEDs.end(), Name, ped_find_pred);
	if (I == m_PEDs.end() || (0 != xr_strcmp((*I)->m_Name, Name)))
		return m_PEDs.end();
	return I;
}

PS::CPEDef* CPSLibrary::FindPED(LPCSTR Name)
{
	PS::PEDIt it = FindPEDIt(Name);
	return (it == m_PEDs.end()) ? 0 : *it;
}

PS::PGDIt CPSLibrary::FindPGDIt(LPCSTR Name)
{
	if (!Name)
		return m_PGDs.end();
	PS::PGDIt I = std::lower_bound(m_PGDs.begin(), m_PGDs.end(), Name, pgd_find_pred);
	if (I == m_PGDs.end() || (0 != xr_strcmp((*I)->m_Name, Name)))
		return m_PGDs.end();
	return I;
}

PS::CPGDef* CPSLibrary::FindPGD(LPCSTR Name)
{
	PS::PGDIt it = FindPGDIt(Name);
	return (it == m_PGDs.end()) ? 0 : *it;
}

bool CPSLibrary::Load(LPCSTR nm)
{
	if (!FS.exist(nm))
	{
		LogInfo("Can't find file: '%s'", nm);
		return false;
	}

	IReader* F = FS.r_open(nm);
	bool bRes = true;
	R_ASSERT(F->find_chunk(PS_CHUNK_VERSION));
	u16 ver = F->r_u16();
	if (ver != PS_VERSION)
	{
		FS.r_close(F);
		return false;
	}

	// second generation: effects
	IReader* OBJ = F->open_chunk(PS_CHUNK_SECONDGEN);
	if (OBJ)
	{
		IReader* O = OBJ->open_chunk(0);
		for (int count = 1; O; ++count)
		{
			PS::CPEDef* def = xr_new<PS::CPEDef>();
			if (def->Load(*O))
				m_PEDs.push_back(def);
			else
			{
				bRes = false;
				xr_delete(def);
			}
			O->close();
			if (!bRes)
				break;
			O = OBJ->open_chunk(count);
		}
		OBJ->close();
	}

	// third generation: groups
	OBJ = F->open_chunk(PS_CHUNK_THIRDGEN);
	if (OBJ)
	{
		IReader* O = OBJ->open_chunk(0);
		for (int count = 1; O; ++count)
		{
			PS::CPGDef* def = xr_new<PS::CPGDef>();
			if (def->Load(*O))
				m_PGDs.push_back(def);
			else
			{
				bRes = false;
				xr_delete(def);
			}
			O->close();
			if (!bRes)
				break;
			O = OBJ->open_chunk(count);
		}
		OBJ->close();
	}

	FS.r_close(F);

	std::sort(m_PEDs.begin(), m_PEDs.end(), ped_sort_pred);
	std::sort(m_PGDs.begin(), m_PGDs.end(), pgd_sort_pred);

	for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
		(*e_it)->CreateShader();

	LogInfo("[BGFX] PS Library loaded: PED=%u PGD=%u", (u32)m_PEDs.size(), (u32)m_PGDs.size());
	return bRes;
}

using PS::CPGDef;

CPGDef const* const* CPSLibrary::particles_group_begin() const
{
	return (m_PGDs.size() ? &*m_PGDs.begin() : 0);
}

CPGDef const* const* CPSLibrary::particles_group_end() const
{
	return (m_PGDs.size() ? &*m_PGDs.end() : 0);
}

void CPSLibrary::particles_group_next(PS::CPGDef const* const*& iterator) const
{
	VERIFY(iterator);
	VERIFY(iterator >= particles_group_begin());
	VERIFY(iterator < particles_group_end());
	++iterator;
}

shared_str const& CPSLibrary::particles_group_id(CPGDef const& particles_group) const
{
	return particles_group.m_Name;
}

CPSLibrary& bgfxPSLibrary()
{
	static CPSLibrary s_lib;
	static bool s_loaded = false;
	if (!s_loaded)
		s_loaded = s_lib.OnCreate();
	return s_lib;
}

particles_systems::library_interface const& bgfxPSLibraryInterface()
{
	return bgfxPSLibrary();
}
