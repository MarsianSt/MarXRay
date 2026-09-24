////////////////////////////////////////////////////////////////////////////
//	Module 		: VisualBulletSystem.cpp
//	Created 	: 24.01.2026
//	Modified 	: 05.03.2026
//	Author		: Dance Maniac (M.F.S. Team)
//	Description : Bullets visualisation system class
//  MIT License
//	Copyright(c) 2026 Dance Maniac
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "VisualBulletSystem.h"
#include "Weapon.h"
#include "WeaponMagazined.h"
#include "WeaponMagazinedWGrenade.h"
#include "player_hud.h"

CVisualBulletSystem::CVisualBulletSystem()
{
	m_pWeapon = nullptr;
	m_bVisualBulletSystem = false;
	m_bAmmoTypesVisuals = false;
	m_bProtectaMode	= false;
	feeder_bone_prefix = nullptr;
	cur_ammo_type = 0;
	next_ammo_type = 0;
	bullet_system_mode = 0;
}

CVisualBulletSystem::~CVisualBulletSystem()
{
}

void CVisualBulletSystem::Init(CWeapon* Weapon)
{
	m_pWeapon = Weapon;
}

void CVisualBulletSystem::Load(LPCSTR section)
{
	m_bVisualBulletSystem = READ_IF_EXISTS(pSettings, r_bool, section, "visual_bullet_system", false);

	if (!m_bVisualBulletSystem || !m_pWeapon)
		return;

	bullet_system_mode = static_cast<u8>(READ_IF_EXISTS(pSettings, r_u32, section, "visual_bullet_mode", 0));

	bullet_bones_in_model.clear();
	bullet_bones_sets.clear();
	shell_bones_sets.clear();

	if (pSettings->line_exist(section, "bullet_bones_in_model"))
	{
		LPCSTR str = pSettings->r_string(section, "bullet_bones_in_model");

		for (int i = 0, count = _GetItemCount(str); i < count; ++i)
		{
			xr_string bone_name;
			_GetItem(str, i, bone_name);
			bullet_bones_in_model.push_back(bone_name);
		}
	}

	switch (bullet_system_mode)
	{
	case eModeMagazined:
		{
			if (pSettings->line_exist(section, "shell_bones_in_model"))
				shell_bones_sets.push_back(pSettings->r_string(section, "shell_bones_in_model"));

			feeder_bone_prefix = READ_IF_EXISTS(pSettings, r_string, section, "feeder_bone_prefix", nullptr);
		} break;
	case eModeShotgun:
	case eModeDoubleBarrel:
		{
			m_bProtectaMode = READ_IF_EXISTS(pSettings, r_bool, section, "protecta_mode", false);

			for (int i = 0; i < static_cast<int>(m_pWeapon->m_ammoTypes.size()); ++i)
			{
				LPCSTR paramName = make_string("bullet_bones_set_%d", i).c_str();
				bullet_bones_sets.push_back(pSettings->r_string(section, paramName));
			}
		} break;
	case eModeRevolver:
		{
			m_bAmmoTypesVisuals = READ_IF_EXISTS(pSettings, r_bool, section, "ammo_types_visuals", false);

			for (int i = 0; i < static_cast<int>(m_pWeapon->m_ammoTypes.size()); ++i)
			{
				LPCSTR paramName = make_string("bullet_bones_set_%d", i).c_str();
				bullet_bones_sets.push_back(pSettings->r_string(section, paramName));

				paramName = make_string("shell_bones_set_%d", i).c_str();
				shell_bones_sets.push_back(pSettings->r_string(section, paramName));
			}
		} break;
	case eModeWeaponGL:
		{
			if (pSettings->line_exist(section, "grenades_bones_by_type"))
			{
				const char* str = pSettings->r_string(section, "grenades_bones_by_type");

				for (int i = 0, count = _GetItemCount(str); i < count;)
				{
					xr_string ammo_section, grenade_bone;
					_GetItem(str, i++, ammo_section);
					R_ASSERT2(i < count, make_string("Incorrect [grenades_bones_by_type] in section [%s]", section).c_str());
					_GetItem(str, i++, grenade_bone);
					grenades_bones_by_type.emplace(std::move(ammo_section), std::move(grenade_bone));
				}
			}
		} break;
	default:
		break;
	}
}

void CVisualBulletSystem::Update(const bool forced, const bool unload_mode)
{
	if (bullet_system_mode != 4 && bullet_bones_in_model.empty())
		return;

	if (!m_pWeapon->GetHUDmode())
		return;

	cur_ammo_type = m_pWeapon->m_ammoType;
	next_ammo_type = m_pWeapon->m_set_next_ammoType_on_reload;

	switch (bullet_system_mode)
	{
	case eModeMagazined:
		{
			ReloadMagazined(forced, unload_mode);
		} break;
	case eModeShotgun:
	case eModeDoubleBarrel:
		{
			ReloadShotgun((forced || m_bProtectaMode), unload_mode);
		} break;
	case eModeRevolver:
		{
			ReloadRevolver(forced, unload_mode);
		} break;
	case eModeWeaponGL:
		{
			if (grenades_bones_by_type.size())
				ReloadWeaponGL(forced, unload_mode);
		} break;
	default:
		break;
	}
}

void CVisualBulletSystem::ReloadShotgun(const bool forced, const bool unload_mode)
{
	if (bullet_bones_sets.empty())
		return;

	const u32 id = (next_ammo_type != CWeapon::undefined_ammo_type) ? next_ammo_type : cur_ammo_type;

	if (id >= bullet_bones_sets.size())
	{
		LogInfo("!! [%s] No bone set for ammoType %d (max: %d)", __FUNCTION__, id, bullet_bones_sets.size() - 1);
		return;
	}

	const auto& bones_to_show = bullet_bones_sets[unload_mode ? cur_ammo_type : id];
	u8 bullets_to_show = static_cast<u8>(m_pWeapon->GetAmmoElapsed());

	if (!forced && current_bullet_bones == bones_to_show)
		return;

	for (size_t i = 0; i < bullet_bones_in_model.size(); ++i)
	{
		u16 bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bullet_bones_in_model[i].c_str());

		if (bone_id != BI_NONE)
		{
			bool should_show = (bullets_to_show && i <= bullets_to_show);
			m_pWeapon->HudItemData()->set_bone_visible(bullet_bones_in_model[i].c_str(), m_bProtectaMode ? should_show : false, TRUE);
		}
	}

	xr_string temp = bones_to_show.c_str();
	for (int i = 0, count = _GetItemCount(temp.c_str()); i < count; ++i)
	{
		string64 bone_name;
		_GetItem(temp.c_str(), i, bone_name);

		u16 bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bone_name);

		if (bone_id != BI_NONE)
			m_pWeapon->HudItemData()->set_bone_visible(bone_name, true, TRUE);
		else
			LogInfo("!! [%s] Bone [%s] not found in model for ammoType %d", __FUNCTION__, bone_name, id);
	}

	current_bullet_bones = bones_to_show;
}

void CVisualBulletSystem::ReloadWeaponGL(const bool forced, const bool unload_mode)
{
	if (grenades_bones_by_type.empty())
		return;

	bool IsWGL = smart_cast<CWeaponMagazinedWGrenade*>(m_pWeapon);
	if (IsWGL && !m_pWeapon->IsGrenadeMode())
		return;

	const u32 id = (next_ammo_type != CWeapon::undefined_ammo_type) ? next_ammo_type : cur_ammo_type;

	const auto& current_ammo_sect = m_pWeapon->m_ammoTypes[id];
	const auto grenade_bone_find_it = grenades_bones_by_type.find(current_ammo_sect.c_str());
	R_ASSERT2(grenade_bone_find_it != grenades_bones_by_type.end(), make_string("!!Can't find [%s] in [grenades_bones_by_type] of [%s]", current_ammo_sect.c_str(), m_pWeapon->cNameSect().c_str()).c_str());
	const auto& grenade_bone_name = grenade_bone_find_it->second;

	if (!forced && current_grenade_bone == grenade_bone_name)
		return;

	for (const auto& bone_pair : grenades_bones_by_type)
	{
		const auto& bone_name = bone_pair.second;
		u16 bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bone_name.c_str());

		if (bone_id != BI_NONE)
			m_pWeapon->HudItemData()->set_bone_visible(bone_name.c_str(), FALSE, TRUE);
	}

	u16 current_bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(grenade_bone_name.c_str());

	if (current_bone_id != BI_NONE)
		m_pWeapon->HudItemData()->set_bone_visible(grenade_bone_name.c_str(), TRUE, TRUE);
	else
		LogInfo("!! [%s] Grenade bone [%s] not found in model", __FUNCTION__, grenade_bone_name.c_str());

	current_grenade_bone = grenade_bone_name;
}

void CVisualBulletSystem::ReloadRevolver(const bool forced, const bool unload_mode)
{
	if (bullet_bones_sets.empty())
		return;

	CWeaponMagazined* m_pWeaponMagazined = smart_cast<CWeaponMagazined*>(m_pWeapon);

	if (!m_pWeaponMagazined)
		return;

	const u32 id = (next_ammo_type != CWeapon::undefined_ammo_type) ? next_ammo_type : cur_ammo_type;

	if (id >= bullet_bones_sets.size())
	{
		LogInfo("!! [%s] No bone set for ammoType %d (max: %d)", __FUNCTION__, id, bullet_bones_sets.size() - 1);
		return;
	}

	// C������� ��� ����� �� bullet_bones_in_model
	for (u8 i = 0; i < bullet_bones_in_model.size(); i++)
	{
		u16 bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bullet_bones_in_model[i].c_str());

		if (bone_id != BI_NONE)
			m_pWeapon->HudItemData()->set_bone_visible(bullet_bones_in_model[i].c_str(), false, TRUE);
	}

	u8 bullets_to_show = static_cast<u8>(unload_mode ? m_pWeaponMagazined->GetAmmoElapsed() : m_pWeaponMagazined->GetAmmoMagSize());

	if (!m_pWeapon->unlimited_ammo() && !unload_mode)
	{
		u8 available = m_pWeaponMagazined->GetAvailableCartridgesToLoad(true);
		bullets_to_show = static_cast<u8>((available >= bullets_to_show) ? bullets_to_show : (available + m_pWeaponMagazined->GetAmmoElapsed()));
	}

	xr_string temp = bullet_bones_sets[unload_mode ? cur_ammo_type : id].c_str();
	for (int i = 0, count = _GetItemCount(temp.c_str()); i < count; ++i)
	{
		string64 bone_name;
		_GetItem(temp.c_str(), i, bone_name);

		u16 bullet_bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bone_name);

		if (bullet_bone_id != BI_NONE)
		{
			bool should_show = unload_mode ? (i < bullets_to_show) : (i >= (count - bullets_to_show));
			m_pWeaponMagazined->HudItemData()->set_bone_visible(bone_name, should_show);
		}
		else
			LogInfo("!! [%s] Bone [%s] not found in model for ammoType %d", __FUNCTION__, bone_name, id);
	}

	temp = shell_bones_sets[unload_mode ? cur_ammo_type : id].c_str();
	for (int i = 0, count = _GetItemCount(temp.c_str()); i < count; ++i)
	{
		string64 bone_name;
		_GetItem(temp.c_str(), i, bone_name);

		u16 spring_bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bone_name);

		if (spring_bone_id != BI_NONE)
		{
			bool spring_visible = unload_mode ? !(i < bullets_to_show) : !(i >= (count - bullets_to_show));
			m_pWeaponMagazined->HudItemData()->set_bone_visible(bone_name, spring_visible);
		}
		else
			LogInfo("!! [%s] Bone [%s] not found in model for ammoType %d", __FUNCTION__, bone_name, id);
	}
}

void CVisualBulletSystem::ReloadMagazined(const bool forced, const bool unload_mode)
{
	CWeaponMagazined* m_pWeaponMagazined = smart_cast<CWeaponMagazined*>(m_pWeapon);

	if (!m_pWeaponMagazined)
		return;

	u8 bullets_to_show = static_cast<u8>(unload_mode ? m_pWeaponMagazined->GetAmmoElapsed() : m_pWeaponMagazined->GetAmmoMagSize());

	if (!m_pWeapon->unlimited_ammo() && !unload_mode)
	{
		u8 available = m_pWeaponMagazined->GetAvailableCartridgesToLoad(true);
		bullets_to_show = static_cast<u8>((available >= bullets_to_show) ? bullets_to_show : (available + m_pWeaponMagazined->GetAmmoElapsed()));
	}

	for (size_t i = 0; i < bullet_bones_in_model.size(); ++i)
	{
		u16 bone_id = m_pWeaponMagazined->HudItemData()->m_model->LL_BoneID(bullet_bones_in_model[i].c_str());

		if (bone_id == BI_NONE)
			continue;

		bool should_show = (bullets_to_show && i <= bullets_to_show);
		m_pWeaponMagazined->HudItemData()->set_bone_visible(bullet_bones_in_model[i].c_str(), should_show);

		if (shell_bones_sets.size())
		{
			xr_string temp = shell_bones_sets[0].c_str();
			for (int item_i = 0, item_count = _GetItemCount(temp.c_str()); item_i < item_count; ++item_i)
			{
				string64 bone_name;
				_GetItem(temp.c_str(), item_i, bone_name);

				u16 spring_bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bone_name);

				if (spring_bone_id != BI_NONE)
				{
					bool spring_visible = !(bullets_to_show && item_i <= bullets_to_show);
					m_pWeaponMagazined->HudItemData()->set_bone_visible(bone_name, spring_visible);
				}
				else
					LogInfo("!! [%s] Bone [%s] not found in model", __FUNCTION__, bone_name);
			}
		}

		if (feeder_bone_prefix && *feeder_bone_prefix)
		{
			for (int feeder_i = 0; feeder_i <= m_pWeaponMagazined->GetAmmoMagSize(); ++feeder_i)
			{
				string64 bone_name{};
				strconcat(sizeof(bone_name), bone_name, feeder_bone_prefix, std::to_string(feeder_i).c_str());

				u16 feeder_bone_id = m_pWeapon->HudItemData()->m_model->LL_BoneID(bone_name);

				if (feeder_bone_id != BI_NONE)
				{
					bool feeder_visible = (i == bullets_to_show);
					m_pWeaponMagazined->HudItemData()->set_bone_visible(bone_name, feeder_visible);
				}
				else
					LogInfo("!! [%s] Bone [%s] not found in model", __FUNCTION__, bone_name);
			}
		}
	}
}