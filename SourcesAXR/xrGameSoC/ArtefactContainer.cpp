////////////////////////////////////////////////////////////////////////////
//	Module 		: ArtefactContainer.cpp
//	Created 	: 08.05.2023
//  Modified 	: 28.03.2026
//	Author		: Dance Maniac (M.F.S. Team)
//	Description : Artefact container
//  MIT License
//	Copyright(c) 2026 Dance Maniac
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ArtefactContainer.h"
#include "artefact_container_script.h"
#include "Artefact.h"
#include "level.h"
#include "Actor.h"

float af_from_container_charge_level = 1.0f;
int af_from_container_rank = 1;
CArtefactContainer* m_LastAfContainer = nullptr;

CArtefactContainer::CArtefactContainer()
{
	m_iContainerSize = 1;
	m_sArtefactsInside.clear();
}

CArtefactContainer::~CArtefactContainer()
{
}

void CArtefactContainer::Load(LPCSTR section)
{
	inherited::Load(section);

	m_iContainerSize = pSettings->r_s32(section, "container_size");
}

BOOL CArtefactContainer::net_Spawn(CSE_Abstract* DC)
{
	return		(inherited::net_Spawn(DC));
}

void CArtefactContainer::save(NET_Packet& packet)
{
	inherited::save(packet);

	u32 numArtefacts = m_sArtefactsInside.size();
	save_data(numArtefacts, packet);

	for (const auto& artefact : m_sArtefactsInside)
	{
		shared_str section = artefact->cNameSect();
		save_data(section, packet);

		artefact->save(packet);
	}
}

void CArtefactContainer::load(IReader& packet)
{
	inherited::load(packet);

	u32 numArtefacts;
	load_data(numArtefacts, packet);

	m_sArtefactsInside.clear();

	for (u32 i = 0; i < numArtefacts; ++i)
	{
		CArtefact* artefact = xr_new<CArtefact>();
		shared_str section;

		load_data(section, packet);

		artefact->Load(section.c_str());

		artefact->load(packet);

		m_sArtefactsInside.push_back(artefact);
	}
}

void CArtefactContainer::PutArtefactToContainer(const CArtefact& artefact)
{
	CArtefact* af = xr_new<CArtefact>(artefact);

	af->m_bInContainer = true;

	m_sArtefactsInside.push_back(af);
}

void CArtefactContainer::TakeArtefactFromContainer(CArtefact* artefact)
{
	for (auto it = m_sArtefactsInside.begin(); it != m_sArtefactsInside.end(); ++it)
	{
		if (*it == artefact)
		{
			af_from_container_charge_level = artefact->GetCurrentChargeLevel();
			af_from_container_rank = artefact->GetCurrentAfRank();

			Level().spawn_item(artefact->cNameSect().c_str(), Position(), false, ID());
			m_sArtefactsInside.erase(it);

			m_LastAfContainer = this;
			return;
		}
	}
}

void CArtefactContainer::TakeArtefactFromContainerBySect(LPCSTR af_section)
{
	for (auto it = m_sArtefactsInside.begin(); it != m_sArtefactsInside.end(); ++it)
	{
		CArtefact* artefact = smart_cast<CArtefact*>(*it);

		if (artefact && xr_strcmp(artefact->cNameSect().c_str(), af_section) == 0)
		{
			af_from_container_charge_level = artefact->GetCurrentChargeLevel();
			af_from_container_rank = artefact->GetCurrentAfRank();

			Level().spawn_item(af_section, Actor()->Position(), false, Actor()->ID());
			m_sArtefactsInside.erase(it);

			m_LastAfContainer = this;
			return;
		}
	}
}

u32 CArtefactContainer::Cost() const
{
	u32 res = CInventoryItem::Cost();

	for (const auto& artefact : m_sArtefactsInside)
		res += artefact->Cost();

	return res;
}

float CArtefactContainer::Weight() const
{
	float res = CInventoryItemObject::Weight();

	for (const auto& artefact : m_sArtefactsInside)
		res += artefact->Weight();

	return res;
}