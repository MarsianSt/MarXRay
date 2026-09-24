////////////////////////////////////////////////////////////////////////////
//	Module 		: WeaponDoubleBarrelShotgun.cpp
//	Created 	: 17.02.2026
//	Modified 	: 18.02.2026
//	Author		: Dance Maniac (M.F.S. Team)
//	Description : Double barrel pump shotguns class
//  MIT License
//	Copyright(c) 2026 Dance Maniac
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "WeaponDoubleBarrelShotgun.h"
#include "WeaponShotgun.h"
#include "xr_level_controller.h"

CWeaponDoubleBarrelShotgun::CWeaponDoubleBarrelShotgun()
{
	m_bIsDoubleBarrelShotgun = true;
}

CWeaponDoubleBarrelShotgun::~CWeaponDoubleBarrelShotgun()
{
}

void CWeaponDoubleBarrelShotgun::Load(LPCSTR section)
{
	inheritedShotgun::Load(section);

	if (WeaponSoundExist(section, "snd_open_weapon_empty_onebullet", true))
		m_sounds.LoadSound(section, "snd_open_weapon_empty_onebullet", "sndOpenEmptyOneBullet", false, m_eSoundOpen);
	if (WeaponSoundExist(section, "snd_open_weapon_empty_twobulletonly", true))
		m_sounds.LoadSound(section, "snd_open_weapon_empty_twobulletonly", "sndOpenEmptyTwoBulletsOnly", false, m_eSoundOpen);

	if (WeaponSoundExist(section, "snd_close_empty_onebullet", true))
		m_sounds.LoadSound(section, "snd_close_empty_onebullet", "sndCloseEmptyOneBullet", false, m_eSoundClose_2);
	if (WeaponSoundExist(section, "snd_close_empty_twobulletonly", true))
		m_sounds.LoadSound(section, "snd_close_empty_twobulletonly", "sndCloseEmptyTwoBulletsOnly", false, m_eSoundClose_2);

	if (WeaponSoundExist(section, "snd_add_cartridge_empty_onebullet", true))
		m_sounds.LoadSound(section, "snd_add_cartridge_empty_onebullet", "sndAddCartridgeEmptyOneBullet", false, m_eSoundAddCartridge);
	if (WeaponSoundExist(section, "snd_add_cartridge_empty_twobulletonly", true))
		m_sounds.LoadSound(section, "snd_add_cartridge_empty_twobulletonly", "sndAddCartridgeEmptyTwoBulletsOnly", false, m_eSoundAddCartridge);

	if (WeaponSoundExist(section, "snd_shoot_r", true))
		m_sounds.LoadSound(section, "snd_shoot_r", "sndShotR", true, m_eSoundShot);

	if (WeaponSoundExist(section, "snd_shoot_actor", true))
		m_sounds.LoadSound(section, "snd_shoot_r_actor", "sndShotRActor", false, m_eSoundShot);

	if (m_bIndoorSoundsEnabled)
	{
		m_sounds.LoadSound(section, "snd_shoot_r_indoor", "sndShotRIndoor", false, m_eSoundShot);

		if (WeaponSoundExist(section, "snd_shoot_actor", true))
			m_sounds.LoadSound(section, "snd_shoot_r_actor_indoor", "sndShotRActorIndoor", false, m_eSoundShot);
	}
}

bool CWeaponDoubleBarrelShotgun::Action(u16 cmd, u32 flags)
{
	if (inheritedPistol::Action(cmd, flags))
		return true;

	bool over_three_cartridge = HaveCartridgeInInventory(3);

	if (over_three_cartridge && m_bTriStateReload && GetState() == eReload && !IsMisfire() && cmd == kWPN_FIRE && flags & CMD_START &&
	m_sub_state == eSubstateReloadInProcess) //���������� ������������
	{
		AddCartridge((over_three_cartridge && !iAmmoElapsed) ? 2 : 1);
		m_sub_state = eSubstateReloadEnd;
		m_bIsCancelReloadNow = true;
		return true;
	}

	return false;
}

void CWeaponDoubleBarrelShotgun::OnAnimationEnd(u32 state)
{
	switch (state)
	{
	case eFire:
		{
			if (IsMisfire())
				SwitchState(eIdle);
		} break;
	}

	if (!m_bTriStateReload || (m_bIsBoltRiffle && !(IsScopeAttached() && m_bOnlyTriStateWithScope) && !iAmmoElapsed && HaveCartridgeInInventory(static_cast<u8>(iMagazineSize))) || state != eReload)
		return inheritedShotgun::OnAnimationEnd(state);

	switch (m_sub_state)
	{
	case eSubstateReloadBegin:
		{
			m_sub_state = static_cast<u8>(IsMisfire() ? eSubstateReloadEnd : eSubstateReloadInProcess);
			SwitchState(eReload);
		} break;
	case eSubstateReloadInProcess:
		{
			if (0 != AddCartridge((HaveCartridgeInInventory(2) && !iAmmoElapsed) ? 2 : 1))
				m_sub_state = eSubstateReloadEnd;

			SwitchState(eReload);
		} break;
	case eSubstateReloadEnd:
		{
			m_sub_state = eSubstateReloadBegin;
			SwitchState(eIdle);
		} break;
	};
}

void CWeaponDoubleBarrelShotgun::switch2_AddCartgidge()
{
	if (iAmmoElapsed == 0)
	{
		if (HaveCartridgeInInventory(3))
			PlaySound("sndAddCartridgeEmpty", get_LastFP());
		else if (HaveCartridgeInInventory(2))
			PlaySound("sndAddCartridgeEmptyTwoBulletsOnly", get_LastFP());
		else
			PlaySound("sndAddCartridgeEmptyOneBullet", get_LastFP());
	}
	else
		PlaySound("sndAddCartridge", get_LastFP());

	PlayAnimAddOneCartridgeWeapon();
	SetPending(TRUE);
}

void CWeaponDoubleBarrelShotgun::switch2_StartReload()
{
	if (!IsMisfire())
		PlaySound((iAmmoElapsed == 0 && m_sounds.FindSoundItem("sndOpenEmpty", false)) ? "sndOpenEmpty" : "sndOpen", get_LastFP());
	else
		PlaySound((iAmmoElapsed == 1 && m_sounds.FindSoundItem("sndReloadMisfireEmpty", false)) ? "sndReloadMisfireEmpty" : "sndReloadMisfire", get_LastFP());

	PlayAnimOpenWeapon();
	SetPending(TRUE);
}

void CWeaponDoubleBarrelShotgun::switch2_EndReload()
{
	SetPending(TRUE);

	if (!IsMisfire())
	{
		if (m_sounds.FindSoundItem("sndCloseEmptyTwoBulletsOnly", false) && iAmmoElapsed == 2)
		{
			if (m_bIsCancelReloadNow && !isHUDAnimationExist("anm_close_empty_twobulletonly"))
				PlaySound("sndClose_2", get_LastFP());
			else
				PlaySound("sndCloseEmptyTwoBulletsOnly", get_LastFP());
		}
		else if (m_sounds.FindSoundItem("sndCloseEmptyOneBullet", false) && iAmmoElapsed == 1)
			PlaySound("sndCloseEmptyOneBullet", get_LastFP());
		else
			PlaySound((iAmmoElapsed == 0 && m_sounds.FindSoundItem("sndClose_2_Empty", false)) ? "sndClose_2_Empty" : "sndClose_2", get_LastFP());

		PlayAnimCloseWeapon();
	}
	else
	{
		bMisfire = false;

		if (GetAmmoElapsed() > 0)
			SetAmmoElapsed(GetAmmoElapsed() - 1);

		SwitchState(eIdle);
	}
}

void CWeaponDoubleBarrelShotgun::PlayAnimShoot()
{
	VERIFY(GetState() == eFire);

	if ((IsRotatingToZoom() && m_zoom_params.m_fZoomRotationFactor != 0.0f) || (IsRotatingFromZoom() && m_zoom_params.m_fZoomRotationFactor != 1.0f))
		return;

	string_path guns_shoot_anm{};
	strconcat(sizeof(guns_shoot_anm), guns_shoot_anm, ((iAmmoElapsed % 2 == 0) ? "anm_shoot_l" : "anm_shoot_r"), (IsZoomed() && !IsRotatingToZoom()) ? (IsScopeAttached() && m_bUseAimScopeAnims ? "_aim_scope" : "_aim") : "", (IsSilencerAttached() && m_bUseSilShotAnim) ? "_sil" : "");

	PlayHUDMotionIfExists({ guns_shoot_anm, "anm_shoot", "anm_shots" }, false, GetState());
}

void CWeaponDoubleBarrelShotgun::PlayAnimAddOneCartridgeWeapon()
{
	VERIFY(GetState() == eReload);

	if (iAmmoElapsed == 0)
	{
		if (HaveCartridgeInInventory(3))
			PlayHUDMotionIfExists({ "anm_add_cartridge_empty", "anm_add_cartridge" }, true, GetState());
		else if (HaveCartridgeInInventory(2))
			PlayHUDMotionIfExists({ "anm_add_cartridge_empty_twobulletonly", "anm_add_cartridge" }, true, GetState());
		else
			PlayHUDMotionIfExists({ "anm_add_cartridge_empty_onebullet", "anm_add_cartridge" }, true, GetState());
	}
	else
	{
		if (iAmmoElapsed % 2 == 0)
			PlayHUDMotion("anm_add_cartridge_l", FALSE, this, GetState());
		else
			PlayHUDMotion("anm_add_cartridge_r", FALSE, this, GetState());
	}

	if (m_bBulletsVisualization && ((iAmmoElapsed + 1) != iMagazineSize))
		HUD_VisualBulletUpdate();
}

void CWeaponDoubleBarrelShotgun::PlayAnimOpenWeapon()
{
	VERIFY(GetState() == eReload);

	if (IsMisfire())
	{
		if (iAmmoElapsed == 1)
			PlayHUDMotionIfExists({ "anm_reload_misfire_empty", "anm_reload_misfire", "anm_close" }, true, GetState());
		else
			PlayHUDMotionIfExists({ "anm_reload_misfire", "anm_close" }, true, GetState());
	}
	else
	{
		if (iAmmoElapsed == 0)
		{
			if (HaveCartridgeInInventory(3))
				PlayHUDMotionIfExists({ "anm_open_empty", "anm_open_weapon", "anm_open" }, false, GetState());
			else if (HaveCartridgeInInventory(2))
				PlayHUDMotionIfExists({ "anm_open_empty_twobulletonly", "anm_open_empty", "anm_open_weapon", "anm_open" }, true, GetState());
			else
				PlayHUDMotionIfExists({ "anm_open_empty_onebullet", "anm_open_empty", "anm_open_weapon", "anm_open" }, true, GetState());
		}
		else
			PlayHUDMotionIfExists({ "anm_open_weapon", "anm_open" }, false, GetState());
	}
}

void CWeaponDoubleBarrelShotgun::PlayAnimCloseWeapon()
{
	VERIFY(GetState() == eReload);

	if (iAmmoElapsed == 2)
	{
		if (m_bIsCancelReloadNow && !isHUDAnimationExist("anm_close_empty_twobulletonly"))
			PlayHUDMotionIfExists({ "anm_close_weapon", "anm_close" }, true, GetState());
		else
			PlayHUDMotionIfExists({ "anm_close_empty_twobulletonly", "anm_close_weapon_empty", "anm_close_empty", "anm_close_weapon", "anm_close" }, true, GetState());
	}
	else if (iAmmoElapsed == 1)
		PlayHUDMotionIfExists({ "anm_close_empty_onebullet", "anm_close_weapon_empty", "anm_close_empty", "anm_close_weapon", "anm_close" }, true, GetState());
	else
		PlayHUDMotionIfExists({ "anm_close_weapon", "anm_close" }, true, GetState());

	if (m_bBulletsVisualization)
		HUD_VisualBulletUpdate();
}
