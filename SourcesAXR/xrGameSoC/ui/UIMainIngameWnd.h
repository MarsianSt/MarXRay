// UIMainIngameWnd.h:  окошки-информация в игре
// 
//////////////////////////////////////////////////////////////////////

#pragma once

#include "UIProgressBar.h"
#include "UIGameLog.h"

#include "alife_space.h"

#include "UICarPanel.h"
#include "UIMotionIcon.h"
#include "../hudsound.h"
#include "../EntityCondition.h"
#include "UIMultiElement.h"

class					CUIPdaMsgListItem;
class					CLAItem;
class					CUIZoneMap;
class					CUIArtefactPanel;
class					CUICompassPanel;
class					CUIScrollView;
struct					GAME_NEWS_DATA;
class					CActor;
class					CWeapon;
class					CMissile;
class					CInventoryItem;
class					CUIHudStatesWnd;
class					CUICellItem;
class					CUIMultiElement;

class CUIMainIngameWnd: public CUIWindow  
{
public:
	CUIMainIngameWnd();
	virtual ~CUIMainIngameWnd();

	virtual void Init();
	virtual void Draw();
	virtual void Update();

	bool OnKeyboardPress(int dik);

protected:
	
	CUIStatic			UIStaticDiskIO;
	CUIStatic			UIStaticHealth;
	CUIStatic			UIStaticArmor;
	CUIStatic			UIStaticQuickHelp;
	CUIProgressBar		UIHealthBar;
	CUIProgressBar		UIArmorBar;
	CUICarPanel			UICarPanel;
	CUIMotionIcon		UIMotionIcon;	
	CUIZoneMap*			UIZoneMap;
	CUICompassPanel*	UICompassPanel;

	CUIStatic*			m_ind_temperature;
	u32					m_min_temperature_clr, m_mid_temperature_clr, m_max_temperature_clr;

	CUIStatic*			m_ind_weather_type;

	CUIMultiElement*	m_ind_boost_psy;
	CUIMultiElement*	m_ind_boost_radia;
	CUIMultiElement*	m_ind_boost_chem;
	CUIMultiElement*	m_ind_boost_wound;
	CUIMultiElement*	m_ind_boost_weight;
	CUIMultiElement*	m_ind_boost_health;
	CUIMultiElement*	m_ind_boost_power;
	CUIMultiElement*	m_ind_boost_rad;
	CUIMultiElement*	m_ind_boost_satiety;
	CUIMultiElement*	m_ind_boost_thirst;
	CUIMultiElement*	m_ind_boost_psy_health;
	CUIMultiElement*	m_ind_boost_intoxication;
	CUIMultiElement*	m_ind_boost_sleepeness;
	CUIMultiElement*	m_ind_boost_alcoholism;
	CUIMultiElement*	m_ind_boost_hangover;
	CUIMultiElement*	m_ind_boost_narcotism;
	CUIMultiElement*	m_ind_boost_withdrawal;

	//иконка, показывающая количество активных PDA
	CUIStatic			UIPdaOnline;
	
	//изображение оружия
	CUIStatic			UIWeaponBack;
	CUIStatic			UIWeaponSignAmmo;
	CUIStatic			UIWeaponIcon;
	Frect				UIWeaponIcon_rect;
public:
	void				DrawMainIndicatorsForInventory	();
	CUIStatic*			GetPDAOnline					() { return &UIPdaOnline; };

	float				hud_info_x;
	float				hud_info_y;

	CGameFont*			m_HudInfoFont;

	float				hud_info_item_x;
	Fvector3			hud_info_item_y_pos;

	Ivector4			hud_info_n;
	Ivector4			hud_info_e;
	Ivector4			hud_info_f;

	Ivector4			ch_info_n;
	Ivector4			ch_info_e;
	Ivector4			ch_info_f;
protected:


	// 5 статиков для отображения иконок:
	// - сломанного оружия
	// - радиации
	// - ранения
	// - голода
	// - усталости
	CUIStatic			UIWeaponJammedIcon;
	CUIStatic			UIRadiaitionIcon;
	CUIStatic			UIWoundIcon;
	CUIStatic			UIStarvationIcon;
	CUIStatic			UIPsyHealthIcon;
	CUIStatic			UIInvincibleIcon;
//	CUIStatic			UISleepIcon;
	CUIStatic			UIArtefactIcon;
	CUIStatic			UIFrostbiteIcon;
	CUIStatic			UIHeatingIcon;

	CUIScrollView*		m_UIIcons;
	CUIWindow*			m_pMPChatWnd;
	CUIWindow*			m_pMPLogWnd;
public:	
	CUIArtefactPanel*    m_artefactPanel;
	
public:
	
	// Енумы соответсвующие предупреждающим иконкам 
	enum EWarningIcons
	{
		ewiAll = 0,
		ewiWeaponJammed,
		ewiRadiation,
		ewiWound,
		ewiFrostbite,
		ewiStarvation,
		ewiPsyHealth,
//		ewiSleep,
		ewiHeating,
		ewiInvincible,
		ewiArtefact,
	};

	void				SetMPChatLog					(CUIWindow* pChat, CUIWindow* pLog);

	// Задаем цвет соответствующей иконке
	void				SetWarningIconColor				(EWarningIcons icon, const u32 cl);
	void				TurnOffWarningIcon				(EWarningIcons icon);

	// Пороги изменения цвета индикаторов, загружаемые из system.ltx
	typedef				xr_map<EWarningIcons, xr_vector<float> >	Thresholds;
	typedef				Thresholds::iterator						Thresholds_it;
	Thresholds			m_Thresholds;

	// Енум перечисления возможных мигающих иконок
	enum EFlashingIcons
	{
		efiPdaTask = 0,
		efiEncyclopedia = 1,
		efiJournal = 2,
		efiMail
	};
	
	void				SetFlashIconState_				(EFlashingIcons type, bool enable);

	void				AnimateContacts					(bool b_snd);
	HUD_SOUND_ITEM		m_contactSnd;

	void				ReceiveNews						(GAME_NEWS_DATA* news);
	void				UpdateMainIndicators			();
	void				UpdateBoosterIndicators			(const xr_map<EBoostParams, SBooster> influences);
	
protected:
	void				SetWarningIconColor				(CUIStatic* s, const u32 cl);
	void				InitFlashingIcons				(CUIXml* node);
	void				DestroyFlashingIcons			();
	void				UpdateFlashingIcons				();
	void				UpdateActiveItemInfo			();

	void				SetAmmoIcon						(const shared_str& seсt_name);

	// first - иконка, second - анимация
	DEF_MAP				(FlashingIcons, EFlashingIcons, CUIStatic*);
	FlashingIcons		m_FlashingIcons;

	//для текущего активного актера и оружия
	CActor*				m_pActor;	
	CWeapon*			m_pWeapon;
	CMissile*			m_pGrenade;
	CInventoryItem*		m_pItem;

	// Отображение подсказок при наведении прицела на объект
	void				RenderQuickInfos();

public:
	CUICarPanel&		CarPanel							(){return UICarPanel;};
	CUIMotionIcon&		MotionIcon							(){return UIMotionIcon;}
	void				OnConnected							();
	void				reset_ui							();
protected:
	CInventoryItem*		m_pPickUpItem;
	float				fuzzyShowInfo_;
	CUICellItem*		uiPickUpItemIconNew_;
	float				m_iPickUpItemIconX;
	float				m_iPickUpItemIconY;
	float				m_iPickUpItemIconScale;
public:
	void				SetPickUpItem	(CInventoryItem* PickUpItem);
};