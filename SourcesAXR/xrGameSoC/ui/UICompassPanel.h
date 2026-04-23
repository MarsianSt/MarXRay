#pragma once

#include "UIWindow.h"
#include "UIStatic.h"

#include "../map_location.h"
#include "../map_location_defs.h"
#include "../map_manager.h"

class CAI_Stalker;
class CSE_ALifeDynamicObject;

class CUICompassPanel : public CUIWindow
{
	typedef CUIWindow inherited;

private:
	struct SCompassPoint
	{
		u16			id;
		CObject*	object;
		Fvector		position;
		shared_str	text;
		u32			color;
		shared_str	spot_texture;
		xr_vector<ui_shader> spot_icons{};
		xr_vector<Frect> tex_rects{};
		bool		show_always;
		bool		show_name;

		SCompassPoint() : id(0), object(nullptr), color(0), show_always(false), show_name(true)
		{
			position.set(0, 0, 0);
		}
	};

	// Стороны света
	struct SCompassDirection
	{
		shared_str		name;
		float			angle;
		u32				color;
		float			offset_y;
		bool			enabled;
		CUIStatic*		ui_element;
		CGameFont*		font;

		SCompassDirection() : angle(0.f), color(0xFFFFFFFF), offset_y(5.0f), enabled(true), ui_element(nullptr), font(nullptr) {}
	};

	xr_vector<SCompassPoint>    m_points;
	xr_map<u16, CUIStatic*>     m_active_points;

	xr_vector<CSE_ALifeDynamicObject*> m_lc_objects{};

	CUIStatic                   m_background;
	CUIStatic                   m_compass;
	CUIStatic*                  m_point_dist;
	CUIStatic*                  m_point_name;

	CUIWindow                   m_clipFrame;

	bool                        m_has_best_point;
	SCompassPoint               m_best_point;
	float                       m_best_point_x;

	bool                        m_visible;
	bool						m_bEnabled;

	float						m_fPointsWidth;
	float						m_fPointsHeight;

	float						m_fMinVisibleDist;
	float						m_fMaxVisibleDist;
	float						m_fBestPointFovAngle;

	const u32 DEF_COLOR = 0xFFE1FFE1;	// 225,255,225

	// Стороны света
	static const int	DIRECTION_COUNT = 8;
	SCompassDirection	m_directions[DIRECTION_COUNT];
	bool				m_directions_inited = false;
	float				m_dir_offset_y;
	float				m_dir_fade_near;

	u32					m_last_update_time{};
	const u32			UPDATE_TIME = 10;

	// Углы сторон света
	static const float	ANGLE_N;	// 0
	static const float	ANGLE_NE;	// PI/4
	static const float	ANGLE_E;	// PI/2
	static const float	ANGLE_SE;	// 3*PI/4
	static const float	ANGLE_S;	// PI
	static const float	ANGLE_SW;	// -3*PI/4
	static const float	ANGLE_W;	// -PI/2
	static const float	ANGLE_NW;	// -PI/4

public:
	CUICompassPanel	();
	virtual ~CUICompassPanel();

	virtual void Update		();
	virtual void Draw		();
	virtual void Show		(bool status);

	bool Init				();
	void InitDirections		(CUIXml& uiXml);

	void AddPoint			(u16 id, CObject* obj, LPCSTR text, CMapLocation* location, bool show_always = false, bool show_name = true);
	void AddPoint			(u16 id, const Fvector& pos, LPCSTR text, CMapLocation* location, bool show_always = false, bool show_name = true);
	void RemovePoint		(u16 id);
	void ClearPoints		();

	void AddPoints			();
	void AddQuestPoint		(u16 id, CSE_ALifeDynamicObject* obj, LPCSTR text, CMapLocation* location, bool is_active = false);
	void AddNPCSpot			(u16 id, CObject* obj, CAI_Stalker* npc, LPCSTR text, CMapLocation* location);
	void AddCustomSpot		(u16 id, CObject* obj, LPCSTR text, CMapLocation* location);

	void AddLCObject		(u16 id);

	void SetVisible			(bool status) { m_visible = status; }
	bool IsShown			() const { return m_visible; }
	bool IsEnbabled			() const { return m_bEnabled; }

private:
	void UpdatePoints		();
	void UpdateActivePoints	();
	void UpdateBestPointInfo();
	void UpdateDirections	();
	void ShowPointInfo		(const SCompassPoint& point, string128 dist_str, Fvector2 text_pos, u32 alpha_color, bool show_text);

	float CalcPointX		(const Fvector& pos) const;
	void FindBestPoint		();
	CUIStatic* CreateOrGetPoint(u16 id, LPCSTR spot_texture);
	bool IsPointVisible		(const SCompassPoint& point, float x) const;

	void SetHeading			(float angle);
};