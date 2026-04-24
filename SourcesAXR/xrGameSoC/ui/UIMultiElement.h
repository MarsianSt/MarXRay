#pragma once

#include "UIWindow.h"
#include "UIStatic.h"
//#include "ui_arrow.h"

class CUIXml;
class CUIStatic;
class CUIProgressBar;
class CUIProgressShape;
//class UIArrow;

class CUIMultiElement : public CUIStatic
{
	typedef CUIStatic inherited;

private:
	CUIStatic*				m_static;
	CUIStatic*				m_value;
	CUIProgressBar*			m_progress;
	CUIProgressShape*		m_progress_shape;
	//UI_Arrow*				m_arrow;
	//UI_Arrow*				m_arrow_shadow;

	float					m_magnitude;
	bool					m_show_sign;
	shared_str				m_unit_str;
	
public:

					CUIMultiElement			();
	virtual			~CUIMultiElement		();
			void	InitFromXml				( CUIXml& xml, LPCSTR path, int index = 0, CUIWindow* pWnd = nullptr, bool pos_by_parent = true);

			void	SetProgress				(float value);
			float	GetProgressPos			();
			void	SetProgressRange		(float min, float max);
			void	SetProgressShape		(float value, float max_value = 0.0f);

			//void	SetArrow				(float value);
			void	SetValue				(float value);

			void	SetIconInfo				(float value, float max_value = 1.0f);

			void	SetColorAnimation		(LPCSTR lanim, bool bCyclic, bool bOnlyAlpha, bool bTextColor, bool bTextureColor);
			void	ResetColorAnimation		();

			void	InitTexture				(LPCSTR texture);
};