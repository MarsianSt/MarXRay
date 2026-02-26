#include "stdafx.h"
#include "WeaponAttaches.h"
#include "../Include/xrRender/Kinematics.h"

WeaponAttach::WeaponAttach()
{
	attach_hud_visual = nullptr;
	attach_world_visual = nullptr;
	m_attach_bone_name = nullptr;
	m_visualHUDName = nullptr;
	m_visualWorldName = nullptr;
	m_newWeaponWorldVisual = nullptr;
	m_newWeaponHUDSect = nullptr;
	m_section = nullptr;
	m_hud_attach_pos.identity();
	m_world_attach_pos.identity();
	hud_attach_scale = 1.0f;
	world_attach_scale = 1.0f;
}

WeaponAttach* WeaponAttach::CreateAttach(shared_str attach_section, xr_vector<WeaponAttach*>& m_attaches)
{
	WeaponAttach* parent_addon = xr_new<WeaponAttach>();
	parent_addon->Load(attach_section);

	m_attaches.push_back(parent_addon);

	return parent_addon;
}

void WeaponAttach::RemoveAttach(shared_str attach_section, xr_vector<WeaponAttach*>& m_attaches)
{
	for (auto attach : m_attaches)
	{
		if (attach->m_section == attach_section)
			xr_delete(attach);
	}
}

void WeaponAttach::UpdateAttachesPosition(IRenderVisual* model, const Fmatrix& parent, bool hud_mode)
{
	IKinematics* kinematics = model->dcast_PKinematics(); 
	
	if (!kinematics)
		return;

	u16 boneID = kinematics->LL_BoneID(m_attach_bone_name);
	
	if (hud_mode)
	{
		Fvector ypr = hud_attach_pos[1];
		ypr.mul(PI / 180.f);
		m_hud_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
		m_hud_attach_offset.translate_over(hud_attach_pos[0]);

		Fmatrix anchorMatrix = kinematics->LL_GetTransform(boneID);
		m_hud_attach_pos.mul(parent, anchorMatrix);
		m_hud_attach_pos.mulB_43(m_hud_attach_offset);

		if (hud_attach_scale != 1.0f)
		{
			Fmatrix scale, trans;
			trans = m_hud_attach_pos;
			float cur_scale = hud_attach_scale;
			scale.scale(cur_scale, cur_scale, cur_scale);
			m_hud_attach_pos.mul(trans, scale);
		}
	}
	else
	{
		Fvector ypr = world_attach_pos[1];
		ypr.mul(PI / 180.f);
		m_world_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
		m_world_attach_offset.translate_over(world_attach_pos[0]);

		Fmatrix anchorMatrix = kinematics->LL_GetTransform(boneID);
		m_world_attach_pos.mul(parent, anchorMatrix);
		m_world_attach_pos.mulB_43(m_world_attach_offset);

		if (world_attach_scale != 1.0f)
		{
			Fmatrix scale, trans;
			trans = m_world_attach_pos;
			float cur_scale = world_attach_scale;
			scale.scale(cur_scale, cur_scale, cur_scale);
			m_world_attach_pos.mul(trans, scale);
		}
	}
	
	if (attach_hud_visual)
	{
		auto hudKinematics = attach_hud_visual->dcast_PKinematics();

		if (hudKinematics)
		{
			hudKinematics->CalculateBones_Invalidate();
			hudKinematics->CalculateBones(TRUE);
		}
	}

	if (attach_world_visual)
	{
		auto worldKinematics = attach_world_visual->dcast_PKinematics();

		if (worldKinematics)
		{
			worldKinematics->CalculateBones_Invalidate();
			worldKinematics->CalculateBones(TRUE);
		}
	}
}

void WeaponAttach::RenderAttach(bool hud_mode)
{
	if (hud_mode)
	{
		if (attach_hud_visual)
		{
			::Render->set_Transform(&m_hud_attach_pos);
			::Render->add_Visual(attach_hud_visual, true);
		}
		else
			attach_hud_visual = ::Render->model_Create(m_visualHUDName.c_str());
	}
	else
	{
		if (attach_world_visual)
		{
			::Render->set_Transform(&m_world_attach_pos);
			::Render->add_Visual(attach_world_visual, true);
		}
		else
			attach_world_visual = ::Render->model_Create(m_visualWorldName.c_str());
	}
}

void WeaponAttach::Load(shared_str attach_sect)
{
	m_section = attach_sect;
	m_attach_bone_name = READ_IF_EXISTS(pSettings, r_string, attach_sect, "attach_bone_hud", "wpn_body");
	world_attach_pos[0] = READ_IF_EXISTS(pSettings, r_fvector3, attach_sect, "world_attach_offset", Fvector({ 0.f, 0.f, 0.f }));
	world_attach_pos[1] = READ_IF_EXISTS(pSettings, r_fvector3, attach_sect, "world_attach_rotation", Fvector({ 0.f, 0.f, 0.f }));
	hud_attach_pos[0] = READ_IF_EXISTS(pSettings, r_fvector3, attach_sect, "hud_attach_offset", Fvector({ 0.f, 0.f, 0.f }));
	hud_attach_pos[1] = READ_IF_EXISTS(pSettings, r_fvector3, attach_sect, "hud_attach_rotation", Fvector({ 0.f, 0.f, 0.f }));
	m_visualHUDName = READ_IF_EXISTS(pSettings, r_string, attach_sect, "attach_hud_visual", nullptr);
	m_visualWorldName = READ_IF_EXISTS(pSettings, r_string, attach_sect, "attach_world_visual", m_visualHUDName);
	hud_attach_scale = READ_IF_EXISTS(pSettings, r_float, attach_sect, "hud_attach_scale", 1.0f);
	world_attach_scale = READ_IF_EXISTS(pSettings, r_float, attach_sect, "world_attach_scale", hud_attach_scale);

	m_newWeaponWorldVisual = READ_IF_EXISTS(pSettings, r_string, attach_sect, "weapon_world_visual", nullptr);
	m_newWeaponHUDSect = READ_IF_EXISTS(pSettings, r_string, attach_sect, "weapon_hud", nullptr);
}