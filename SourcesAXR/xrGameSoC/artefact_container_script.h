////////////////////////////////////////////////////////////////////////////
//	Module 		: artefact_container_script.cpp
//	Created 	: 28.03.2026
//  Modified 	: 28.03.2026
//	Author		: Dance Maniac (M.F.S. Team)
//	Description : Artefact container script export
//  MIT License
//	Copyright(c) 2026 Dance Maniac
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "pch_script.h"
#include "ArtefactContainer.h"

using namespace luabind;

#pragma optimize("s",on)
void CArtefactContainer::script_register(lua_State* L)
{
	module(L)
		[
			class_<CArtefactContainer, CGameObject>("CArtefactContainer")
				.def(constructor<>())

				.def("get_container_size",						&CArtefactContainer::GetContainerSize)
				.def("set_container_size",						&CArtefactContainer::SetContainerSize)

				.def("get_artefacts_inside",					&CArtefactContainer::GetArtefactsInside, return_stl_iterator)
				.def("is_full",									&CArtefactContainer::IsFull)

				.def("put_artefact_to_container",				&CArtefactContainer::PutArtefactToContainer)
				.def("take_artefact_from_container",			&CArtefactContainer::TakeArtefactFromContainer)
				.def("take_artefact_from_container_by_sect",	&CArtefactContainer::TakeArtefactFromContainerBySect)
		];
}