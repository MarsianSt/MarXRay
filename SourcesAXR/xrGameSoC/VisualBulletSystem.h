#pragma once

#include "Weapon.h"

class CVisualBulletSystem
{
	CWeapon*				m_pWeapon;
	bool					m_bVisualBulletSystem;
	xr_string				current_bullet_bones{};
	xr_vector<xr_string>	bullet_bones_in_model{};
	xr_vector<xr_string>	bullet_bones_sets{};

	// Grenade Launchers
	xr_string				current_grenade_bone{};
	string_unordered_map<xr_string, xr_string> grenades_bones_by_type{};

	// Magazined & Revolvers
	xr_vector<xr_string>	shell_bones_sets{};
	LPCSTR					feeder_bone_prefix{};
	bool					m_bAmmoTypesVisuals{};
	bool					m_bProtectaMode{};

	u8						cur_ammo_type{};
	u8						next_ammo_type{};
	u8						bullet_system_mode{};

public:
							CVisualBulletSystem();
							~CVisualBulletSystem();

	enum EModes {
		eModeMagazined = 0,
		eModeShotgun,
		eModeDoubleBarrel,
		eModeRevolver,
		eModeWeaponGL
	};

	void					Init(CWeapon* Weapon);
	void					Load(LPCSTR section);
	void					Update(const bool forced = false, const bool unload_mode = false);
	void					ReloadRevolver(const bool forced = false, const bool unload_mode = false);
	void					ReloadShotgun(const bool forced = false, const bool unload_mode = false);
	void					ReloadMagazined(const bool forced = false, const bool unload_mode = false);
	void					ReloadWeaponGL(const bool forced = false, const bool unload_mode = false);
};

