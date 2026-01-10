#pragma once

#include "../Include/xrRender/KinematicsAnimated.h"
#include "Weapon.h"

class WeaponAttach
{
public:
	WeaponAttach();
	~WeaponAttach() {};
	IRenderVisual*	attach_hud_visual;
	IRenderVisual*	attach_world_visual;
	Fvector			hud_attach_pos[2];
	Fvector			world_attach_pos[2];
	Fmatrix			m_hud_attach_pos;
	Fmatrix			m_hud_attach_offset;
	Fmatrix			m_world_attach_pos;
	Fmatrix			m_world_attach_offset;

	float			hud_attach_scale;
	float			world_attach_scale;

	shared_str		m_attach_bone_name;		// Позиция кости которая будет использоваться для аттача, если пусто то аттачим к кости wpn_body
	shared_str		m_visualHUDName;		// Путь до меша
	shared_str		m_visualWorldName;		// Путь до меша мировой модели (опционально)
	shared_str		m_section;				// Название секции

	WeaponAttach* CreateAttach(shared_str attach_section, xr_vector<WeaponAttach*>& m_attaches);
	void RemoveAttach(shared_str attach_section, xr_vector<WeaponAttach*>& m_attaches);
	void UpdateAttachesPosition(IRenderVisual* model, const Fmatrix& parent, bool hud_mode = true);
	void RenderAttach(bool hud_mode = true);
	void Load(shared_str attach_sect);
};