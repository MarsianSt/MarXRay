#include "stdafx.h"
#pragma hdrstop

#include "r_constants.h"

// BGFX port: no HLSL reflection/setup happens, so the constant table is empty.

R_constant_table::~R_constant_table	()
{
	clear();
}

void	R_constant_table::fatal			(LPCSTR S)
{
	FATAL	(S);
}

ref_constant R_constant_table::get	(LPCSTR S)
{
	for (c_table::iterator it = table.begin(); it != table.end(); ++it)
		if (xr_strcmp((*it)->name, S) == 0)
			return *it;
	return 0;
}

ref_constant R_constant_table::get	(shared_str& S)
{
	return get(S.c_str());
}

void	R_constant_table::clear		()
{
	table.clear();
}

void	R_constant_table::merge		(R_constant_table* C)
{
	if (C)
		for (c_table::iterator it = C->table.begin(); it != C->table.end(); ++it)
			table.push_back(*it);
}

BOOL	R_constant_table::equal		(R_constant_table& C)
{
	if (table.size() != C.table.size())
		return FALSE;
	for (u32 i = 0; i < table.size(); ++i)
		if (table[i] != C.table[i])
			return FALSE;
	return TRUE;
}

BOOL	R_constant_table::parse		(void* desc, u32 destination)
{
	return FALSE;	// no reflection in BGFX port
}