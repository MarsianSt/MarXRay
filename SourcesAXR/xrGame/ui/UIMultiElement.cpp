////////////////////////////////////////////////////////////////////////////
//	Module 		: UIMultiElement.cpp
//	Created 	: 24.04.2026
//	Modified 	: 05.05.2026
//	Author		: Dance Maniac (M.F.S. Team)
//	Description : Multifunctional element for user interface
//  MIT License
//	Copyright(c) 2026 Dance Maniac
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "UIMultiElement.h"
#include "UIXmlInit.h"
#include "UIHelper.h"
#include "UIProgressBar.h"
#include "UIProgressShape.h"
#include "ui_arrow.h"
#include "../string_table.h"

CUIMultiElement::CUIMultiElement()
{
	m_static		= nullptr;
	m_progress		= nullptr;
	m_progress_shape = nullptr;
	m_arrow			= nullptr;
	m_arrow_shadow	= nullptr;
	m_value			= nullptr;

	m_magnitude		= 1.0f;
	m_show_sign		= false;
	m_unit_str._set	("");
}

CUIMultiElement::~CUIMultiElement()
{
}

void CUIMultiElement::InitFromXml(CUIXml& xml, LPCSTR path, int index, CUIWindow* pWnd, bool pos_by_parent)
{
	if (!xml.NavigateToNode(path, index))
		return;

	CUIXmlInit::InitWindow(xml, path, index, this);

	if (pWnd)
	{
		pWnd->AttachChild(this);
		SetAutoDelete(true);

		if (pos_by_parent)
			SetWndPos(pWnd->GetWndPos());
	}

	if ((m_static = UIHelper::CreateStatic(xml, path, this, false)))
	{
		XML_NODE* stored_root = xml.GetLocalRoot();
		XML_NODE* new_root = xml.NavigateToNode(path, index);
		xml.SetLocalRoot(new_root);

		if (xml.NavigateToNode("progress_bar", index))
		{
			m_progress = UIHelper::CreateProgressBar(xml, "progress_bar", m_static);
		}

		if (xml.NavigateToNode("value", index))
		{
			m_value = UIHelper::CreateTextWnd(xml, "value", m_static);
			m_magnitude = xml.ReadAttribFlt("value", 0, "magnitude", 1.0f);
			m_show_sign = (xml.ReadAttribInt("value", 0, "show_sign", 1) == 1);

			LPCSTR unit_str = xml.ReadAttrib("value", 0, "unit_str", "");
			m_unit_str._set(CStringTable().translate(unit_str));
		}

		if (xml.NavigateToNode("progress_shape", index))
		{
			m_progress_shape = UIHelper::CreateProgressShape(xml, "progress_shape", m_static, false);
		}

		if (xml.NavigateToNode("arrow", 0))
		{
			m_arrow = UIHelper::CreateArrow(xml, "arrow", m_static, false);
		}

		if (xml.NavigateToNode("arrow_shadow", 0))
		{
			m_arrow_shadow = UIHelper::CreateArrow(xml, "arrow_shadow", m_static, false);
		}

		xml.SetLocalRoot(stored_root);
	}
}

void CUIMultiElement::SetProgress(float value)
{
	if (!m_progress)
		return;

	m_progress->SetProgressPos(value);
}

void CUIMultiElement::SetProgressShape(float value, float max_value)
{
	if (!m_progress_shape)
		return;

	if (fis_zero(max_value))
		m_progress_shape->SetPos(value);
	else
		m_progress_shape->SetPos(static_cast<int>(value), static_cast<int>(max_value));
}

float CUIMultiElement::GetProgressPos()
{
	if (!m_progress)
		return 0.f;

	m_progress->GetProgressPos();
}

void CUIMultiElement::SetProgressRange(float min, float max)
{
	if (!m_progress)
		return;

	m_progress->SetRange(min, max);
}

void CUIMultiElement::SetArrow(float value)
{
	if (!m_arrow)
		return;

	m_arrow->SetNewValue(value);

	if (!m_arrow_shadow)
		return;

	m_arrow_shadow->SetPos(m_arrow->GetPos());
}

void CUIMultiElement::SetValue(float value)
{
	if (!m_value)
		return;

	value *= m_magnitude;
	string32 buf;
	xr_sprintf(buf, "%.0f", value);

	LPCSTR str;

	if (m_unit_str.size())
		STRCONCAT(str, buf, " ", m_unit_str.c_str());
	else
		STRCONCAT(str, buf);

	m_value->SetText(str);
}

void CUIMultiElement::SetIconInfo(float value, float max_value)
{
	if (m_value)
		SetValue(value);

	if (m_progress)
	{
		m_progress->SetProgressPos(value);
		m_progress->SetRange(0.f, max_value);
	}

	if (m_progress_shape)
		m_progress_shape->SetPos(static_cast<int>(value * 100.f), static_cast<int>(max_value * 100.f));

	if (m_arrow)
		SetArrow(value);
}

void CUIMultiElement::SetColorAnimation(LPCSTR lanim, u8 const& flags, float delay)
{
	if (m_static)
		m_static->SetColorAnimation(lanim, flags, delay);

	if (m_value)
		m_value->SetColorAnimation(lanim, flags, delay);

	if (m_progress)
		m_progress->SetColorAnimation(lanim, flags, delay);

	if (m_progress_shape)
		m_progress_shape->SetColorAnimation(lanim, flags, delay);

	if (m_arrow)
		m_arrow->SetColorAnimation(lanim, flags, delay);

	if (m_arrow_shadow)
		m_arrow_shadow->SetColorAnimation(lanim, flags, delay);
}

void CUIMultiElement::ResetColorAnimation()
{
	if (m_static)
		m_static->ResetColorAnimation();

	if (m_value)
		m_value->ResetColorAnimation();

	if (m_progress)
		m_progress->ResetColorAnimation();

	if (m_progress_shape)
		m_progress_shape->ResetColorAnimation();

	if (m_arrow)
		m_arrow->ResetColorAnimation();

	if (m_arrow_shadow)
		m_arrow_shadow->ResetColorAnimation();
}

void CUIMultiElement::InitTexture(LPCSTR texture)
{
	if (m_static)
		m_static->InitTexture(texture);
}