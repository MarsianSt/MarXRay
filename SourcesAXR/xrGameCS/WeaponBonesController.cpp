////////////////////////////////////////////////////////////////////////////
//	Module 		: WeaponBonesController.cpp
//	Created 	: 22.03.2026
//	Modified 	: 22.03.2026
//	Author		: Dance Maniac (M.F.S. Team)
//	Description : Weapon bones transform and controller classes
//  MIT License
//	Copyright(c) 2026 Dance Maniac
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "WeaponBonesController.h"
#include "object_broker.h"
#include "Weapon.h"
#include "../Include/xrRender/Kinematics.h"

CWeaponBoneTransform::CWeaponBoneTransform()
{
	m_sBoneName = nullptr;
	m_bInitialized = false;
	m_RotationAxis.set(0.f, 0.f, 0.f);
	m_fRotationSpeed = 1000.0f;
	m_fCurrentAngle = PI_MUL_2;
	m_fTargetAngle = PI_MUL_2;
	m_Rotation.identity();

	m_fTranslationSpeed = 1.0f;
	m_CurrentTranslation.set(0.f, 0.f, 0.f);
	m_TargetTranslation.set(0.f, 0.f, 0.f);
	m_Translation.identity();
	m_FullTransform.identity();

	m_iTransformMode = 0;
}

CWeaponBoneTransform::~CWeaponBoneTransform()
{
}

void CWeaponBoneTransform::Load(LPCSTR section, shared_str bone_name, u32 fire_modes_cnt, u32 cur_fire_mode)
{
	m_sBoneName = bone_name;

	if (!m_sBoneName.size())
		return;

	string128 rot_axis_param{}, rot_speed_param{}, trans_speed_param{}, transform_ctrl_mode_param{};
	xr_sprintf(rot_axis_param, "%s_rot_axis", m_sBoneName.c_str());
	xr_sprintf(rot_speed_param, "%s_rot_speed", m_sBoneName.c_str());
	xr_sprintf(trans_speed_param, "%s_trans_speed", m_sBoneName.c_str());
	xr_sprintf(transform_ctrl_mode_param, "%s_transform_ctrl_mode", m_sBoneName.c_str());

	m_RotationAxis = READ_IF_EXISTS(pSettings, r_fvector3, section, rot_axis_param, Fvector().set(0.f, 0.f, 0.f));
	m_fRotationSpeed = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, rot_speed_param, 1000.0f));
	m_fTranslationSpeed = READ_IF_EXISTS(pSettings, r_float, section, trans_speed_param, 0.5f);
	m_iTransformMode = READ_IF_EXISTS(pSettings, r_u32, section, transform_ctrl_mode_param, 0);

	m_RotationAngles.clear();
	m_TranslationOffsets.clear();

	switch (m_iTransformMode)
	{
	case eModeDefault:
		{
			string128 rot_angle_param{};
			xr_sprintf(rot_angle_param, "%s_rot_angle", m_sBoneName.c_str());
			float angle = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, rot_angle_param, 0.0f));
			m_RotationAngles.push_back(angle);

			m_fTargetAngle = m_RotationAngles[0];
			m_fCurrentAngle = m_fTargetAngle;

			string128 trans_offset_param{};
			xr_sprintf(trans_offset_param, "%s_trans_offset", m_sBoneName.c_str());
			Fvector translation = READ_IF_EXISTS(pSettings, r_fvector3, section, trans_offset_param, Fvector().set(0.f, 0.f, 0.f));
			m_TranslationOffsets.push_back(translation);

			m_TargetTranslation = m_TranslationOffsets[0];
			m_CurrentTranslation = m_TargetTranslation;
		} break;
	case eModeFiremode:
		{
			for (u32 i = 0; i < fire_modes_cnt; i++)
			{
				string128 param_name{};
				xr_sprintf(param_name, "%s_rot_angle_mode_%d", m_sBoneName.c_str(), i);

				float angle = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, param_name, 0.0f));
				m_RotationAngles.push_back(angle);
			}

			m_fTargetAngle = m_RotationAngles[cur_fire_mode];
			m_fCurrentAngle = m_fTargetAngle;

			for (u32 i = 0; i < fire_modes_cnt; i++)
			{
				string128 param_name{};
				xr_sprintf(param_name, "%s_trans_offset_mode_%d", m_sBoneName.c_str(), i);
				Fvector translation = READ_IF_EXISTS(pSettings, r_fvector3, section, param_name, Fvector().set(0.f, 0.f, 0.f));
				m_TranslationOffsets.push_back(translation);
			}

			m_TargetTranslation = m_TranslationOffsets[cur_fire_mode];
			m_CurrentTranslation = m_TargetTranslation;
		} break;
	default:
		break;
	}

	ApplyBoneMatrix();

	m_bInitialized = true;
}

void CWeaponBoneTransform::Update()
{
	if (!m_bInitialized)
		return;

	bool need_update = false;

	// ========== Rotation ==========
	if (!fsimilar(m_fCurrentAngle, m_fTargetAngle))
	{
		float delta = m_fRotationSpeed * Device.fTimeDelta;
		float diff = m_fTargetAngle - m_fCurrentAngle;

		if (_abs(diff) <= delta)
			m_fCurrentAngle = m_fTargetAngle;
		else
			m_fCurrentAngle += (diff > 0 ? delta : -delta);

		need_update = true;
	}

	// ========== Translation ==========
	if (!m_CurrentTranslation.similar(m_TargetTranslation))
	{
		Fvector delta_trans;
		delta_trans.sub(m_TargetTranslation, m_CurrentTranslation);
		float dist = delta_trans.magnitude();
		float step = m_fTranslationSpeed * Device.fTimeDelta;

		if (dist <= step)
			m_CurrentTranslation = m_TargetTranslation;
		else
		{
			delta_trans.normalize();
			delta_trans.mul(step);
			m_CurrentTranslation.add(delta_trans);
		}

		need_update = true;
	}

	if (need_update)
		ApplyBoneMatrix();
}

void CWeaponBoneTransform::BoneCallback(CBoneInstance* P)
{
	CWeaponBoneTransform* transform = static_cast<CWeaponBoneTransform*>(P->callback_param());
	P->mTransform.mulB_43(transform->m_FullTransform);
}

void CWeaponBoneTransform::SetBoneCallback(IKinematics* visual)
{
	if (!visual)
		return;

	u16 boneId = visual->LL_BoneID(m_sBoneName);
	if (boneId == BI_NONE)
		return;

	CBoneInstance& safetyBone = visual->LL_GetBoneInstance(boneId);
	safetyBone.set_callback(bctCustom, &BoneCallback, this);
}

void CWeaponBoneTransform::ResetBoneCallback(IKinematics* visual)
{
	if (!visual)
		return;

	u16 boneId = visual->LL_BoneID(m_sBoneName);
	if (boneId == BI_NONE)
		return;

	CBoneInstance& safetyBone = visual->LL_GetBoneInstance(boneId);
	safetyBone.reset_callback();
}

void CWeaponBoneTransform::ApplyBoneMatrix()
{
	m_Rotation.identity();
	m_Rotation.rotation(m_RotationAxis, m_fCurrentAngle);

	m_Translation.identity();
	if (!m_CurrentTranslation.similar(Fvector().set(0.f, 0.f, 0.f)))
		m_Translation.translate(m_CurrentTranslation);

	// Сначала поворот, потом сдвиг (порядок важен)
	m_FullTransform.mul(m_Rotation, m_Translation);
}

void CWeaponBoneTransform::RefreshBoneCurrentParams()
{
	m_fTargetAngle = m_RotationAngles[0];
	m_fCurrentAngle = m_fTargetAngle;
	m_TargetTranslation = m_TranslationOffsets[0];
	m_CurrentTranslation = m_TargetTranslation;

	ApplyBoneMatrix();
}

// Установка режима стрельбы для предохранителей
void CWeaponBoneTransform::SetSafetyMode(u32 mode, u32 modes_count)
{
	if (!m_RotationAngles.empty() && mode < modes_count && mode < m_RotationAngles.size())
		m_fTargetAngle = m_RotationAngles[mode];

	if (m_TranslationOffsets.size() && mode < m_TranslationOffsets.size())
		m_TargetTranslation = m_TranslationOffsets[mode];
}

////////////////////////////////////////////////////////////////////////////
// CWeaponBoneController
////////////////////////////////////////////////////////////////////////////

CWeaponBonesController::CWeaponBonesController()
{
}

CWeaponBonesController::~CWeaponBonesController()
{
	delete_data(m_Bones);
}

void CWeaponBonesController::AddBoneTransform(shared_str bone_name)
{
	if (!bone_name.size())
		return;

	if (m_Bones.find(bone_name) != m_Bones.end())
		return;

	m_Bones[bone_name] = xr_new<CWeaponBoneTransform>();
}

CWeaponBoneTransform* CWeaponBonesController::GetBoneTransform(shared_str bone_name)
{
	if (!bone_name.size())
		return nullptr;

	auto it = m_Bones.find(bone_name);

	if (it != m_Bones.end())
		return it->second;

	return nullptr;
}

void CWeaponBonesController::RemoveBoneTransform(shared_str bone_name)
{
	if (!bone_name.size())
		return;

	auto it = m_Bones.find(bone_name);

	if (it != m_Bones.end())
	{
		xr_delete(it->second);
		m_Bones.erase(it);
	}
}

void CWeaponBonesController::Load(LPCSTR section, u32 fire_modes_cnt, u32 cur_fire_mode)
{
	if (!pSettings->section_exist(section))
		return;

	if (pSettings->line_exist(section, "controlled_bones"))
	{
		shared_str controlled_bones_list = pSettings->r_string(section, "controlled_bones");
		int bones_count = _GetItemCount(controlled_bones_list.c_str());

		m_Bones.clear();
		for (size_t i = 0; i < bones_count; i++)
		{
			string128 bone_name;
			_GetItem(controlled_bones_list.c_str(), i, bone_name);

			if (m_Bones.find(bone_name) != m_Bones.end())
				continue;

			m_Bones.insert(std::make_pair(bone_name, xr_new<CWeaponBoneTransform>()));
			m_Bones[bone_name]->Load(section, bone_name, fire_modes_cnt, cur_fire_mode);
		}
	}
}

void CWeaponBonesController::SetCallbacks(IKinematics* visual)
{
	if (!visual)
		return;

	for (auto& bone_pair : m_Bones)
		bone_pair.second->SetBoneCallback(visual);
}

void CWeaponBonesController::ResetCallbacks(IKinematics* visual)
{
	if (!visual)
		return;

	for (auto& bone_pair : m_Bones)
		bone_pair.second->ResetBoneCallback(visual);
}

void CWeaponBonesController::Update()
{
	for (auto& bone_pair : m_Bones)
		bone_pair.second->Update();
}

void CWeaponBonesController::SetTransformMode(shared_str bone_name, u32 mode)
{
	if (!bone_name.size())
		return;

	if (m_Bones.find(bone_name) == m_Bones.end())
		return;

	m_Bones[bone_name]->SetTransformMode(mode);
}

void CWeaponBonesController::RefreshBoneCurrentParams(shared_str bone_name)
{
	if (!bone_name.size())
		return;

	if (m_Bones.find(bone_name) == m_Bones.end())
		return;

	m_Bones[bone_name]->RefreshBoneCurrentParams();
}

// Установка режима стрельбы для предохранителей
void CWeaponBonesController::SetSafetyMode(u32 mode, u32 modes_count)
{
	for (auto& bone : m_Bones)
	{
		if (bone.second->GetTransformMode() == CWeaponBoneTransform::eModeFiremode)
			bone.second->SetSafetyMode(mode, modes_count);
	}
}