#pragma once

#include "WeaponMagazined.h"

void ShowHudEditor(bool& show);
void EditBonesTransform(CWeaponMagazined* WpnMag, float drag_intensity = 0.0001f);
bool HudEditor_MouseWheel(float wheel);
