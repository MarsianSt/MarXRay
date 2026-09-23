////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_object_registry.cpp
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife object registry
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_object_registry.h"
#include "ai_debug.h"
#include "../xrCore/TaskManager.h"
#include "object_factory.h"

CALifeObjectRegistry::CALifeObjectRegistry	(LPCSTR section)
{
}

CALifeObjectRegistry::~CALifeObjectRegistry	()
{
	OBJECT_REGISTRY::iterator const B	= m_objects.begin();
	OBJECT_REGISTRY::iterator I			= B;
	OBJECT_REGISTRY::iterator const E	= m_objects.end();
	for ( ; I != E; ++I)
		(*I).second->on_unregister	();

	for (I = B; I != E; ++I)
		xr_delete					((*I).second);
}

void CALifeObjectRegistry::save				(IWriter &memory_stream, CSE_ALifeDynamicObject *object, u32 &object_count)
{
	++object_count;

	NET_Packet					tNetPacket;
	// Spawn
	object->Spawn_Write			(tNetPacket,TRUE);
	memory_stream.w_u16			(u16(tNetPacket.B.count));
	memory_stream.w				(tNetPacket.B.data,tNetPacket.B.count);

	// Update
	tNetPacket.w_begin			(M_UPDATE);
	object->UPDATE_Write		(tNetPacket);

	memory_stream.w_u16			(u16(tNetPacket.B.count));
	memory_stream.w				(tNetPacket.B.data,tNetPacket.B.count);

	ALife::OBJECT_VECTOR::const_iterator	I = object->children.begin();
	ALife::OBJECT_VECTOR::const_iterator	E = object->children.end();
	for ( ; I != E; ++I) {
		CSE_ALifeDynamicObject	*child = this->object(*I,true);
		if (!child)
			continue;

		if (!child->can_save())
			continue;

		save					(memory_stream,child,object_count);
	}
}

void CALifeObjectRegistry::save				(IWriter &memory_stream)
{
	LogInfo("* Saving objects...");
	memory_stream.open_chunk	(OBJECT_CHUNK_DATA);

	u32							position = memory_stream.tell();
	memory_stream.w_u32			(u32(-1));

	u32							object_count = 0;
	OBJECT_REGISTRY::iterator	I = m_objects.begin();
	OBJECT_REGISTRY::iterator	E = m_objects.end();
	for ( ; I != E; ++I) {
		if (!(*I).second->can_save())
			continue;

		if ((*I).second->redundant())
			continue;

		if ((*I).second->ID_Parent != 0xffff)
			continue;

		save					(memory_stream,(*I).second, object_count);
	}
	
	u32							last_position = memory_stream.tell();
	memory_stream.seek			(position);
	memory_stream.w_u32			(object_count);
	memory_stream.seek			(last_position);

	memory_stream.close_chunk	();
	
	LogInfo("* %d objects are successfully saved",object_count);
}

CSE_ALifeDynamicObject *CALifeObjectRegistry::get_object		(IReader &file_stream)
{
	NET_Packet				tNetPacket;
	u16						u_id;
	// Spawn
	tNetPacket.B.count		= file_stream.r_u16();
	file_stream.r			(tNetPacket.B.data,tNetPacket.B.count);
	tNetPacket.r_begin		(u_id);
	R_ASSERT2				(M_SPAWN==u_id,"Invalid packet ID (!= M_SPAWN)");

	string64				s_name;
	tNetPacket.r_stringZ	(s_name);
#ifdef DEBUG
	if (psAI_Flags.test(aiALife)) {
		LogInfo("Loading object %s [%d]b", s_name, tNetPacket.B.count);
	}
#endif
	// create entity
	CSE_Abstract			*tpSE_Abstract = F_entity_Create	(s_name);
	R_ASSERT2				(tpSE_Abstract,"Can't create entity.");
	CSE_ALifeDynamicObject	*tpALifeDynamicObject = smart_cast<CSE_ALifeDynamicObject*>(tpSE_Abstract);
	R_ASSERT2				(tpALifeDynamicObject,"Non-ALife object in the saved game!");
	tpALifeDynamicObject->Spawn_Read(tNetPacket);

	// Update
	tNetPacket.B.count		= file_stream.r_u16();
	file_stream.r			(tNetPacket.B.data,tNetPacket.B.count);
	tNetPacket.r_begin		(u_id);
	R_ASSERT2				(M_UPDATE==u_id,"Invalid packet ID (!= M_UPDATE)");
	tpALifeDynamicObject->UPDATE_Read(tNetPacket);

	return					(tpALifeDynamicObject);
}

void CALifeObjectRegistry::load				(IReader &file_stream)
{ 
	LogInfo("* Loading objects...");
	R_ASSERT2					(file_stream.find_chunk(OBJECT_CHUNK_DATA),"Can't find chunk OBJECT_CHUNK_DATA!");

	m_objects.clear				();

	u32							count = file_stream.r_u32();
	CSE_ALifeDynamicObject		**objects = (CSE_ALifeDynamicObject**)_alloca(count*sizeof(CSE_ALifeDynamicObject*));

	if (count)
	{
		u8							*base = (u8*)file_stream.pointer() - file_stream.tell();

		struct SObjectSlice {
			u32						field_offset;
			u32						slice_size;
			u16						spawn_size;
			bool					scripted;
		};

		xr_vector<SObjectSlice>		slices;
		slices.resize				(count);
		for (u32 i=0; i<count; ++i) {
			SObjectSlice&			slice = slices[i];
			slice.field_offset		= file_stream.tell();
			slice.spawn_size		= file_stream.r_u16();
			file_stream.seek		(file_stream.tell() + slice.spawn_size);
			u16						update_size = file_stream.r_u16();
			file_stream.seek		(file_stream.tell() + update_size);
			slice.slice_size		= file_stream.tell() - slice.field_offset;

			IReader					spawn_stream(base + slice.field_offset + 2, slice.spawn_size);
			u16						packet_id = spawn_stream.r_u16();
			R_ASSERT2				(M_SPAWN==packet_id,"Invalid packet ID (!= M_SPAWN)");
			string64				s_name;
			spawn_stream.r_stringZ	(s_name, sizeof(s_name));
			slice.scripted			= object_factory().is_script_object(pSettings->r_clsid(s_name,"class"));
		}

		auto load_object = [&](u32 i) {
			SObjectSlice&			slice = slices[i];
			IReader					sub_stream(base + slice.field_offset, (int)slice.slice_size);
			objects[i]				= get_object(sub_stream);
		};

	if (!CTaskManager::IsInsideTask() && (count >= 64)) {
			CTaskManager::AddTaskRange([&](u32 start, u32 end, u32) {
				for (u32 i=start; i<end; ++i)
					if (!slices[i].scripted)
						load_object(i);
			}, count, 16);
			CTaskManager::WaitAll	();

			// Scripted objects create their server entities through Lua creators,
			// which are not thread-safe - keep them on the main thread.
			for (u32 i=0; i<count; ++i)
				if (slices[i].scripted)
					load_object(i);
		}
		else {
			for (u32 i=0; i<count; ++i)
				load_object(i);
		}
	}

	CSE_ALifeDynamicObject		**I = objects;
	CSE_ALifeDynamicObject		**E = objects + count;
	for ( ; I != E; ++I) {
		add						(*I);
	}

	LogInfo("* %d objects are successfully loaded",count);
}
