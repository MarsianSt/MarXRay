#pragma once

#include "WeaponShotgun.h"
#include "WeaponCustomPistol.h"
#include "script_export_space.h"

class CWeaponDoubleBarrelShotgun : public CWeaponShotgun
{
	typedef CWeaponShotgun inheritedShotgun;
	typedef CWeaponCustomPistol inheritedPistol;

public:
					CWeaponDoubleBarrelShotgun();
	virtual			~CWeaponDoubleBarrelShotgun();

	virtual void	Load				(LPCSTR section) override;

	virtual void	switch2_StartReload	() override;
	virtual void	switch2_AddCartgidge() override;
	virtual void	switch2_EndReload	() override;
	virtual void	PlayAnimAddOneCartridgeWeapon() override;
	virtual void	PlayAnimShoot		() override;
	virtual void	PlayAnimOpenWeapon	() override;
	virtual void	PlayAnimCloseWeapon	() override;

	virtual bool	Action				(u16 cmd, u32 flags) override;

protected:
	virtual void	OnAnimationEnd		(u32 state) override;

	DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CWeaponDoubleBarrelShotgun)
#undef script_type_list
#define script_type_list save_type_list(CWeaponDoubleBarrelShotgun)
