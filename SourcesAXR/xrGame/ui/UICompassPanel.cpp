////////////////////////////////////////////////////////////////////////////
//	Module 		: UICompassPanel.cpp
//	Created 	: 12.03.2026
//	Modified 	: 23.04.2026
//	Author		: Dance Maniac (M.F.S. Team)
//	Description : Horizontal direction indicator
//  MIT License
//	Copyright(c) 2026 Dance Maniac
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "UICompassPanel.h"
#include "Level.h"
#include "Actor.h"
#include "string_table.h"
#include "UIXmlInit.h"
#include "alife_object_registry.h"
#include "../alife_simulator.h"
#include "GametaskManager.h"
#include "GameTask.h"
#include "level_changer.h"
#include "ai/stalker/ai_stalker.h"

CUICompassPanel::CUICompassPanel() : m_point_dist(nullptr), m_point_name(nullptr), m_has_best_point(false), m_best_point_x(0.f), m_visible(true)
{
	float pda_radius	= READ_IF_EXISTS(pSettings, r_float, "device_pda", "radius", 50.f);

	m_fMinVisibleDist	= pda_radius * 0.5f;// Дистанция максимальной видимости
	m_fMaxVisibleDist	= pda_radius;		// Дистанция начала появления
	m_fBestPointFovAngle = deg2rad(25.f);	// Угол для показа информации о точки
	m_dir_offset_y		= 25.0f;			// Общее смещение по Y
	m_dir_fade_near		= 0.15f;			// Граница затухания
	m_fPointsWidth		= 12.f;				// Ширина точки
	m_fPointsHeight		= 12.f;				// Высота точки
	m_bEnabled			= true;				// Включен в xml
}

CUICompassPanel::~CUICompassPanel()
{
	ClearPoints();

	if (m_point_dist)
		xr_delete(m_point_dist);
	if (m_point_name)
		xr_delete(m_point_name);
}

bool CUICompassPanel::Init()
{
	CUIXml uiXml;

	if (!uiXml.Load(CONFIG_PATH, UI_PATH, "ui_axr_compass.xml"))
		return false;

	CUIXmlInit xml_init;

	SetWndPos(Fvector2().set(0, 0));
	SetWndSize(Fvector2().set(1024, 256));

	if (!(m_bEnabled = !!uiXml.ReadAttribInt("compass_panel", 0, "enabled", 1)))
		return false;

	// Панель
	CUIStatic* panel = xr_new<CUIStatic>();
	panel->SetAutoDelete(true);
	panel->SetWndPos(Fvector2().set(0, 0));
	panel->SetWndSize(Fvector2().set(1024, 256));
	AttachChild(panel);

	// Фон
	xml_init.InitStatic(uiXml, "compass_panel:background", 0, &m_background);
	panel->AttachChild(&m_background);

	// Контейнер точек
	xml_init.InitWindow(uiXml, "compass_panel:points_frame", 0, &m_clipFrame);
	panel->AttachChild(&m_clipFrame);

	// Размер точек
	m_fPointsWidth = uiXml.ReadAttribFlt("compass_panel:points_frame", 0, "points_width", 12.f);
	m_fPointsHeight = uiXml.ReadAttribFlt("compass_panel:points_frame", 0, "points_height", 12.f);
	m_fMinVisibleDist = uiXml.ReadAttribFlt("compass_panel:points_frame", 0, "full_visibility_dist", m_fMinVisibleDist);
	m_fMaxVisibleDist = uiXml.ReadAttribFlt("compass_panel:points_frame", 0, "max_visible_dist", m_fMaxVisibleDist);
	m_fBestPointFovAngle = deg2rad(uiXml.ReadAttribFlt("compass_panel:points_frame", 0, "best_point_angle", 25.f));

	// Компас
	xml_init.InitStatic(uiXml, "compass_panel:compass", 0, &m_compass);
	panel->AttachChild(&m_compass);

	// Панель опасностей
	if (uiXml.NavigateToNode("compass_panel:danger_panel", 0))
	{
		CUIStatic* danger_panel = xr_new<CUIStatic>();
		danger_panel->SetAutoDelete(true);
		xml_init.InitStatic(uiXml, "compass_panel:danger_panel", 0, danger_panel);
		danger_panel->Show(false);
		panel->AttachChild(danger_panel);
	}

	// Расстояние до точки
	if (uiXml.NavigateToNode("compass_panel:point_dist", 0))
	{
		m_point_dist = xr_new<CUIStatic>();
		m_point_dist->SetAutoDelete(false);
		xml_init.InitStatic(uiXml, "compass_panel:point_dist", 0, m_point_dist);
		m_point_dist->TextItemControl()->SetText("");
		m_point_dist->TextItemControl()->SetTextAlignment(CGameFont::alCenter);
		m_point_dist->TextItemControl()->SetVTextAlignment(valCenter);
		m_clipFrame.AttachChild(m_point_dist);
	}

	// Название точки
	if (uiXml.NavigateToNode("compass_panel:point_name", 0))
	{
		m_point_name = xr_new<CUIStatic>();
		m_point_name->SetAutoDelete(false);
		xml_init.InitStatic(uiXml, "compass_panel:point_name", 0, m_point_name);

		m_point_name->TextItemControl()->SetText("");
		m_point_name->TextItemControl()->SetTextAlignment(CGameFont::alCenter);
		m_point_name->TextItemControl()->SetVTextAlignment(valCenter);
		m_clipFrame.AttachChild(m_point_name);
	}

	if (uiXml.NavigateToNode("compass_panel:directions", 0))
		InitDirections(uiXml);

	return true;
}

void CUICompassPanel::InitDirections(CUIXml& uiXml)
{
	if (m_directions_inited)
		return;

	static const char* dir_xml_names[] = {
		"north", "northeast", "east", "southeast",
		"south", "southwest", "west", "northwest"
	};

	static const char* default_dir_names[] = {
		"ui_st_compass_n",	// Север
		"ui_st_compass_ne",	// Северо-восток
		"ui_st_compass_e",	// Восток
		"ui_st_compass_se",	// Юго-восток
		"ui_st_compass_s",	// Юг
		"ui_st_compass_sw",	// Юго-запад
		"ui_st_compass_w",	// Запад
		"ui_st_compass_nw"	// Северо-запад
	};

	float default_angles[] = {
		deg2rad(0.0f),		// N  - Север
		deg2rad(45.0f),		// NE - Северо-восток
		deg2rad(90.0f),		// E  - Восток
		deg2rad(135.0f),	// SE - Юго-восток
		deg2rad(180.0f),	// S  - Юг
		deg2rad(225.0f),	// SW - Юго-запад (или -135)
		deg2rad(270.0f),	// W  - Запад  (или -90)
		deg2rad(315.0f)		// NW - Северо-запад (или -45)
	};

	float global_offset_y = uiXml.ReadAttribFlt("compass_panel:directions", 0, "offset_y", 25.0f);
	float global_fade_near = uiXml.ReadAttribFlt("compass_panel:directions", 0, "fade_near", 0.15f);

	m_dir_offset_y = global_offset_y;
	m_dir_fade_near = global_fade_near;

	for (int i = 0; i < DIRECTION_COUNT; i++)
	{
		string64 path{};
		xr_sprintf(path, "compass_panel:directions:%s", dir_xml_names[i]);

		LPCSTR str_id = uiXml.ReadAttrib(path, 0, "str_id", default_dir_names[i]);
		m_directions[i].name = CStringTable().translate(str_id);
		m_directions[i].angle = uiXml.ReadAttribFlt(path, 0, "angle", default_angles[i]);

		float offset_y = uiXml.ReadAttribFlt(path, 0, "offset_y", global_offset_y);
		m_directions[i].offset_y = offset_y;
		m_directions[i].enabled = uiXml.ReadAttribInt(path, 0, "enabled", 1) != 0;
		CUIXmlInit::InitFont(uiXml, path, 0, m_directions[i].color, m_directions[i].font, DEF_COLOR, "letterica16");

		if (m_directions[i].enabled)
		{
			CUIStatic* st = xr_new<CUIStatic>();
			st->SetAutoDelete(false);
			st->TextItemControl()->SetText(m_directions[i].name.c_str());
			st->TextItemControl()->SetTextColor(m_directions[i].color);
			st->TextItemControl()->SetFont(m_directions[i].font);
			st->TextItemControl()->SetTextAlignment(CGameFont::alCenter);
			st->TextItemControl()->SetVTextAlignment(valCenter);
			st->AdjustWidthToText();
			st->SetHeight(20.0f);

			m_clipFrame.AttachChild(st);
			m_directions[i].ui_element = st;
		}
		else
			m_directions[i].ui_element = nullptr;
	}

	m_directions_inited = true;
}

void CUICompassPanel::Update()
{
	if ((Device.dwTimeGlobal - m_last_update_time) < UPDATE_TIME)
		return;

	if (!m_visible || !Actor())
	{
		if (m_point_dist)
			m_point_dist->Show(false);

		if (m_point_name)
			m_point_name->Show(false);

		if (m_directions_inited)
		{
			for (int i = 0; i < DIRECTION_COUNT; i++)
			{
				if (m_directions[i].ui_element)
					m_directions[i].ui_element->Show(false);
			}
		}

		return;
	}

	float h, p;
	Device.vCameraDirection.getHP(h, p);
	SetHeading(-h);

	AddPoints();
	UpdatePoints();
	UpdateActivePoints();
	UpdateDirections();
	FindBestPoint();
	UpdateBestPointInfo();

	m_clipFrame.Update();
	m_background.Update();

	inherited::Update();

	m_last_update_time = Device.dwTimeGlobal;
}

void CUICompassPanel::Draw()
{
	if (!m_visible || !Actor())
		return;

	m_clipFrame.Draw();
	m_background.Draw();
}

void CUICompassPanel::Show(bool status)
{
	m_visible = status;
	inherited::Show(status);
}

void CUICompassPanel::SetHeading(float angle)
{
	m_compass.SetHeading(angle);
}

void CUICompassPanel::AddPoint(u16 id, CObject* obj, LPCSTR text, CMapLocation* location, bool show_always, bool show_name)
{
	for (const auto& point : m_points)
	{
		if (point.id == id)
			return;
	}

	SCompassPoint point;
	point.id = id;
	point.object = obj;
	point.text = text;
	point.color = location->GetCompassSpotColor();
	point.spot_texture = location->GetCompassSpotTextureName().c_str();
	point.spot_icons = location->GetCompassSpotIconShaders();
	point.tex_rects = location->GetCompassSpotTexRects();
	point.show_always = show_always;
	point.show_name = show_name;

	m_points.push_back(point);
}

void CUICompassPanel::AddPoint(u16 id, const Fvector& pos, LPCSTR text, CMapLocation* location, bool show_always, bool show_name)
{
	for (const auto& point : m_points)
	{
		if (point.id == id)
			return;
	}

	SCompassPoint point;
	point.id = id;
	point.position = pos;
	point.text = text;
	point.color = location->GetCompassSpotColor();
	point.spot_texture = location->GetCompassSpotTextureName().c_str();
	point.spot_icons = location->GetCompassSpotIconShaders();
	point.tex_rects = location->GetCompassSpotTexRects();
	point.show_always = show_always;
	point.show_name = show_name;

	m_points.push_back(point);
}

void CUICompassPanel::RemovePoint(u16 id)
{
	for (auto it = m_points.begin(); it != m_points.end(); ++it)
	{
		if (it->id == id)
		{
			m_points.erase(it);
			break;
		}
	}

	auto active_it = m_active_points.find(id);

	if (active_it != m_active_points.end())
	{
		m_clipFrame.DetachChild(active_it->second);
		xr_delete(active_it->second);
		m_active_points.erase(active_it);
	}
}

void CUICompassPanel::ClearPoints()
{
	m_points.clear();
	m_lc_objects.clear();

	for (auto& pair : m_active_points)
	{
		m_clipFrame.DetachChild(pair.second);
		xr_delete(pair.second);
	}
	m_active_points.clear();

	m_has_best_point = false;

	// Стороны света просто скрыть
	if (m_directions_inited)
	{
		for (int i = 0; i < DIRECTION_COUNT; i++)
		{
			if (m_directions[i].ui_element && m_directions[i].enabled)
				m_directions[i].ui_element->Show(false);
		}
	}
}

void CUICompassPanel::AddLCObject(u16 id)
{
	for (auto& lc_obj : m_lc_objects)
	{
		if (lc_obj->ID == id)
			return;
	}

	m_lc_objects.push_back(ai().alife().objects().object(id, true));
}

void CUICompassPanel::AddPoints()
{
	if (!smart_cast<CActor*>(Level().CurrentViewEntity()))
		return;

	ClearPoints();

	for (const auto& map_loc : Level().MapManager().Locations())
	{
		CMapLocation* map_location = map_loc.location;
		u16 object_id = map_location->ObjectID();
		shared_str spot_type = map_loc.spot_type;
		bool enabled = map_location->SpotEnabled() && map_location->GetCompassAvail();
		CObject* obj = Level().Objects.net_Find(object_id);

		for (auto& point : m_points)
		{
			if (point.id == object_id)
				continue;
		}

		const char* str = spot_type.c_str();
		bool is_quest = (strstr(str, "storyline_task") == str || strstr(str, "secondary_task") == str);

		if (!obj && !is_quest)
			continue;

		if (enabled)
		{
			CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(obj);
			CLevelChanger* level_changer = smart_cast<CLevelChanger*>(obj);

			if (is_quest)
			{
				CGameTask* active_task = Level().GameTaskManager().ActiveTask();
				CGameTask* task = Level().GameTaskManager().HasGameTask(map_location, false);

				if (!task)
					continue;

				u16 map_object_id = task->GetMapObjectID();

				if (map_object_id == u16(-1))
					continue;

				CSE_ALifeDynamicObject* alife_obj = ai().alife().objects().object(map_object_id, true);

				if (!alife_obj)
					continue;

				bool is_active = (active_task && (task->m_ID == active_task->m_ID));

				if (map_location->GetLevelName() == Level().name())
				{
					AddQuestPoint(object_id, alife_obj, *CStringTable().translate(task->m_Title), map_location, is_active);
				}
				else
				{
					float best_distance = FLT_MAX;
					alife_obj = nullptr;

					for (const auto& lc_object : m_lc_objects)
					{
						float dist = lc_object->Position().distance_to(Device.vCameraPosition);

						if (dist < best_distance)
						{
							best_distance = dist;
							alife_obj = lc_object;
						}
					}

					if (!alife_obj)
						continue;

					AddQuestPoint(object_id, alife_obj, *CStringTable().translate(task->m_Title), map_location, is_active);
				}
			}
			else if (stalker)
			{
				if (xr_strcmp(str, "ui_pda2_scout_location") == 0)
				{
					AddLCObject(obj->ID());
				}

				AddNPCSpot(object_id, obj, stalker, map_location->GetHint(), map_location);
			}
			else
			{
				bool is_suitable_lc = level_changer && level_changer->IsLevelChangerEnabled() && (level_changer->GetNextLevelName() == map_location->GetLevelName());
				
				if (is_suitable_lc)
				{
					AddLCObject(obj->ID());
				}

				AddCustomSpot(object_id, obj, map_location->GetHint(), map_location);
			}
		}
	}
}

void CUICompassPanel::AddQuestPoint(u16 id, CSE_ALifeDynamicObject* obj, LPCSTR text, CMapLocation* location, bool is_active)
{
	AddPoint(id, obj->Position(), text, location, is_active);
}

void CUICompassPanel::AddNPCSpot(u16 id, CObject* obj, CAI_Stalker* npc, LPCSTR text, CMapLocation* location)
{
	bool is_dead = npc->g_Alive();

	AddPoint(id, obj, text, location, false, is_dead);
}

void CUICompassPanel::AddCustomSpot(u16 id, CObject* obj, LPCSTR text, CMapLocation* location)
{
	AddPoint(id, obj, text, location, false);
}

float CUICompassPanel::CalcPointX(const Fvector& pos) const
{
	Fvector dir;
	dir.sub(pos, Device.vCameraPosition);
	dir.normalize_safe();

	float dir_h = dir.getH();
	float cam_h = Device.vCameraDirection.getH();

	float angle = dir_h - cam_h;

	while (angle > PI)
		angle -= 2 * PI;

	while (angle < -PI)
		angle += 2 * PI;

	float x = 0.5f - angle / (PI);

	return clampr(x, 0.f, 1.f);
}

void CUICompassPanel::UpdatePoints()
{
	// Удаление уничтоженных точек
	for (auto it = m_points.begin(); it != m_points.end();)
	{
		if (it->object && it->object->getDestroy())
			it = m_points.erase(it);
		else
			++it;
	}
}

void CUICompassPanel::UpdateActivePoints()
{
	Fvector cam_pos = Device.vCameraPosition;

	for (auto& point : m_points)
	{
		Fvector pos = point.object ? point.object->Position() : point.position;
		float x = CalcPointX(pos);

		if (x < 0 || x > 1)
		{
			auto it = m_active_points.find(point.id);

			if (it != m_active_points.end())
				it->second->Show(false);

			continue;
		}

		CUIStatic* st = CreateOrGetPoint(point.id, point.spot_texture.c_str());

		if (!st)
			continue;

		if (point.spot_icons[1]->inited() && point.spot_icons[2]->inited())
		{
			float ml_y = pos.y;
			float d = Device.vCameraPosition.y - ml_y;

			if (d > 1.8f)
			{
				st->SetShader(point.spot_icons[2]);
				st->SetTextureRect(point.tex_rects[2]);
			}
			else
			{
				if (d < -1.8f)
				{
					st->SetShader(point.spot_icons[1]);
					st->SetTextureRect(point.tex_rects[1]);
				}
				else
				{
					st->SetShader(point.spot_icons[0]);
					st->SetTextureRect(point.tex_rects[0]);
				}
			}
		}

		float pos_x = m_clipFrame.GetWidth() * x - st->GetWidth() / 2;
		st->SetWndPos(Fvector2().set(pos_x, 0.f));

		float alpha_factor = 1.0f;
		float dist = pos.distance_to(Device.vCameraPosition);

		// Для show_always только краевое затухание
		if (point.show_always)
		{
			alpha_factor = 1.0f;
		}
		else
		{
			alpha_factor = remapval(dist, m_fMinVisibleDist, m_fMaxVisibleDist, 1.0f, 0.0f);
			alpha_factor = clampr(alpha_factor, 0.0f, 1.0f);
		}

		// Краевое затухание
		float edge_factor = 1.0f;
		if (x < 0.2f)
			edge_factor = remapval(x, 0.0f, 0.2f, 0.0f, 1.0f);
		else if (x > 0.8f)
			edge_factor = remapval(x, 0.8f, 1.0f, 1.0f, 0.0f);

		alpha_factor *= edge_factor;

		u8 alpha = u8(clampr(alpha_factor * 255.f, 0.f, 255.f));
		u32 final_color = subst_alpha(point.color, alpha);
		st->SetTextureColor(final_color);
		st->Show(alpha_factor > 0.01f);

		string128 dist_str;
		xr_sprintf(dist_str, "%.0fm", dist);

		if (point.show_always)
			ShowPointInfo(point, dist_str, st->GetWndPos(), final_color, (alpha_factor > 0.01f));
	}
}

void CUICompassPanel::FindBestPoint()
{
	m_has_best_point = false;
	float closest_dist = FLT_MAX;

	for (const auto& point : m_points)
	{
		Fvector pos = point.object ? point.object->Position() : point.position;
		float x = CalcPointX(pos);

		if (x < 0 || x > 1)
			continue;

		float dist = pos.distance_to(Device.vCameraPosition);
		if (!point.show_always && dist >= m_fMaxVisibleDist)
			continue;

		// Проверка фокуса
		Fvector dir_to_point;
		dir_to_point.sub(pos, Device.vCameraPosition);
		dir_to_point.normalize_safe();

		Fvector cam_dir = Device.vCameraDirection;
		cam_dir.normalize_safe();

		float cos_angle = cam_dir.dotproduct(dir_to_point);
		float angle_to_point = acosf(clampr(cos_angle, -1.f, 1.f));

		if (!point.show_always && angle_to_point > m_fBestPointFovAngle)
			continue;

		if (dist < closest_dist)
		{
			closest_dist = dist;
			m_best_point = point;
			m_best_point_x = x;
			m_has_best_point = true;
		}
	}
}

void CUICompassPanel::UpdateBestPointInfo()
{
	if (!m_has_best_point || !(m_point_dist && m_point_name))
	{
		if (m_point_dist)
			m_point_dist->Show(false);

		if (m_point_name)
			m_point_name->Show(false);

		return;
	}

	Fvector pos = m_best_point.object ? m_best_point.object->Position() : m_best_point.position;
	float dist = pos.distance_to(Device.vCameraPosition);
	bool dist_ok = m_best_point.show_always || (dist < m_fMaxVisibleDist);

	if (!dist_ok)
	{
		if (m_point_dist)
			m_point_dist->Show(false);

		if (m_point_name)
			m_point_name->Show(false);

		return;
	}

	CUIStatic* point_st = nullptr;
	auto it = m_active_points.find(m_best_point.id);
	if (it != m_active_points.end())
		point_st = it->second;

	if (!point_st || !point_st->IsShown())
	{
		if (m_point_dist)
			m_point_dist->Show(false);

		if (m_point_name)
			m_point_name->Show(false);

		return;
	}

	float alpha_factor = 1.0f;
	if (!m_best_point.show_always)
	{
		alpha_factor = remapval(dist, m_fMinVisibleDist, m_fMaxVisibleDist, 1.0f, 0.0f);
		alpha_factor = clampr(alpha_factor, 0.0f, 1.0f);
	}

	// Краевое затухание
	float edge_factor = 1.0f;
	if (m_best_point_x < 0.2f)
		edge_factor = remapval(m_best_point_x, 0.0f, 0.2f, 0.0f, 1.0f);
	else if (m_best_point_x > 0.8f)
		edge_factor = remapval(m_best_point_x, 0.8f, 1.0f, 1.0f, 0.0f);

	alpha_factor *= edge_factor;

	u8 alpha = u8(clampr(alpha_factor * 255.f, 0.f, 255.f));
	u32 alpha_color = subst_alpha(m_best_point.color, alpha);
	Fvector2 text_pos = point_st->GetWndPos();

	string128 dist_str;
	xr_sprintf(dist_str, "%.0fm", dist);
	bool show_text = (alpha_factor > 0.01f);

	ShowPointInfo(m_best_point, dist_str, text_pos, alpha_color, show_text);
}

void CUICompassPanel::ShowPointInfo(const SCompassPoint& point, string128 dist_str, Fvector2 text_pos, u32 alpha_color, bool show_text)
{
	if (m_point_dist)
	{
		m_point_dist->TextItemControl()->SetText(dist_str);
		m_point_dist->TextItemControl()->SetTextColor(alpha_color);
		m_point_dist->AdjustWidthToText();
		m_point_dist->SetWndPos(text_pos);
		m_point_dist->SetTextureColor(alpha_color);
		m_point_dist->Show(show_text);
	}

	if (m_point_name)
	{
		if (point.text.size() && point.show_name)
		{
			m_point_name->TextItemControl()->SetText(point.text.c_str());
			m_point_name->TextItemControl()->SetTextColor(alpha_color);
			m_point_name->AdjustWidthToText();
			m_point_name->SetWndPos(text_pos);
			m_point_name->SetTextureColor(alpha_color);
			m_point_name->Show(show_text);
		}
		else
			m_point_name->Show(false);
	}
}

void CUICompassPanel::UpdateDirections()
{
	if (!m_directions_inited)
		return;

	float cam_h = Device.vCameraDirection.getH();

	for (int i = 0; i < DIRECTION_COUNT; i++)
	{
		if (!m_directions[i].enabled)
			continue;

		CUIStatic* st = m_directions[i].ui_element;

		if (!st)
			continue;

		float dir_h = m_directions[i].angle;
		float angle = dir_h - cam_h;

		while (angle > PI)
			angle -= 2 * PI;
		while (angle < -PI)
			angle += 2 * PI;

		float x = 0.5f - angle / (PI);
		x = clampr(x, 0.f, 1.f);
		float pos_x = m_clipFrame.GetWidth() * x - st->GetWidth() / 2;
		st->SetWndPos(Fvector2().set(pos_x, m_directions[i].offset_y));

		float alpha_factor = 1.0f;

		if (x < m_dir_fade_near)
			alpha_factor = x / m_dir_fade_near;
		else if (x > (1.0f - m_dir_fade_near))
			alpha_factor = (1.0f - x) / m_dir_fade_near;

		u8 alpha = u8(clampr(alpha_factor * 255.f, 40.f, 255.f));
		u32 final_color = subst_alpha(m_directions[i].color, alpha);

		st->TextItemControl()->SetTextColor(final_color);
		st->Show(alpha_factor > 0.05f);
	}
}

CUIStatic* CUICompassPanel::CreateOrGetPoint(u16 id, LPCSTR spot_texture)
{
	auto it = m_active_points.find(id);

	if (it != m_active_points.end())
		return it->second;

	CUIStatic* st = xr_new<CUIStatic>();
	st->SetAutoDelete(false);
	st->InitTexture(spot_texture);
	st->SetWndSize(Fvector2().set(m_fPointsWidth * UI().get_current_kx(), m_fPointsHeight));
	st->SetStretchTexture(true);

	m_clipFrame.AttachChild(st);
	m_active_points[id] = st;

	return st;
}

bool CUICompassPanel::IsPointVisible(const SCompassPoint& point, float x) const
{
	if (x < 0 || x > 1)
		return false;

	if (point.show_always)
		return true;

	return true;
}