#pragma once

#include "stdafx.h"

class CWeaponBoneTransform
{
	friend class CWeaponBonesController;
public:
	CWeaponBoneTransform();
	~CWeaponBoneTransform();

	enum EModes {
		eModeDefault = 0,
		eModeFiremode,
	};

	void                Load(LPCSTR section, shared_str bone_name, u32 fire_modes_cnt = 0, u32 cur_fire_mode = 0);

	void                Update();

	u32					GetTransformMode() const { return m_iTransformMode; };
	void                SetTransformMode(u32 mode) { m_iTransformMode = mode; };
	void				RefreshBoneCurrentParams();

	void				SetSafetyMode(u32 mode, u32 modes_count);
	bool                IsInitialized() const { return m_bInitialized; }
	LPCSTR              GetBoneName() const { return m_sBoneName.c_str(); }

	// Управление колбэками
	static void         BoneCallback(CBoneInstance* P);
	void                SetBoneCallback(IKinematics* visual);
	void                ResetBoneCallback(IKinematics* visual);

	float				GetCurrentAngle() const { return m_fCurrentAngle; }
	float				GetTargetAngle() const { return m_fTargetAngle; }
	float				GetRotationSpeed() const { return m_fRotationSpeed; }
	void				SetRotationSpeed(float speed) { m_fRotationSpeed = speed; }

	const Fvector&		GetCurrentTranslation() const { return m_CurrentTranslation; }
	const Fvector&		GetTargetTranslation() const { return m_TargetTranslation; }
	float				GetTranslationSpeed() const { return m_fTranslationSpeed; }
	void				SetTranslationSpeed(float speed) { m_fTranslationSpeed = speed; }

	const xr_vector<float>& GetRotationAngles() const { return m_RotationAngles; }
	xr_vector<float>&	GetRotationAngles() { return m_RotationAngles; }

	const xr_vector<Fvector>& GetTranslationOffsets() const { return m_TranslationOffsets; }
	xr_vector<Fvector>&	GetTranslationOffsets() { return m_TranslationOffsets; }

	const Fvector&		GetRotationAxis() const { return m_RotationAxis; }
	void				SetRotationAxis(const Fvector& axis) { m_RotationAxis = axis; m_RotationAxis.normalize_safe(); }

protected:
	void                ApplyBoneMatrix();

	// Параметры трансформации кости
	shared_str				m_sBoneName;
	Fmatrix					m_FullTransform; // Общая матрица (поворот + сдвиг)
	bool					m_bInitialized;

	// Поворот
	Fmatrix					m_Rotation;
	xr_vector<float>		m_RotationAngles{};
	Fvector					m_RotationAxis;
	float					m_fRotationSpeed;
	float					m_fCurrentAngle;
	float					m_fTargetAngle;

	// Сдвиг
	Fmatrix					m_Translation;
	xr_vector<Fvector>		m_TranslationOffsets{};
	Fvector					m_CurrentTranslation;
	Fvector					m_TargetTranslation;
	float					m_fTranslationSpeed;

	u32						m_iTransformMode;
};

class CWeaponBonesController
{
public:
	CWeaponBonesController();
	~CWeaponBonesController();

	void                Load(LPCSTR section, u32 fire_modes_cnt = 0, u32 cur_fire_mode = 0);
	void                Update();

	// Установка режима работы для конкретной кости
	void                SetTransformMode(shared_str bone_name, u32 mode);

	// Управление колбэками для всех костей
	void                SetCallbacks(IKinematics* visual);
	void                ResetCallbacks(IKinematics* visual);

	void                AddBoneTransform(shared_str bone_name);
	CWeaponBoneTransform* GetBoneTransform(shared_str bone_name);
	void                RemoveBoneTransform(shared_str bone_name);
	void				RefreshBoneCurrentParams(shared_str bone_name);

	// Установка режима стрельбы для предохранителей
	void				SetSafetyMode(u32 mode, u32 modes_count);

	const xr_map<shared_str, CWeaponBoneTransform*>& GetBones() const { return m_Bones; }

private:
	xr_map<shared_str, CWeaponBoneTransform*> m_Bones;
};