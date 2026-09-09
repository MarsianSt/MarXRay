
#pragma once

#define	MTL_EXPORT_API
#define ENGINE_API
#define DLL_API		
#define ECORE_API
//#include "../xrEngine/stdafx.h"

#include "../xrCore/xrCore.h"

#include "../xrServerEntities/smart_cast.h"
//#include "../xrEngine/pure.h"
//#include "../xrEngine/engineapi.h"
//#include "../xrEngine/eventapi.h"


#include "../xrcdb/xrcdb.h"
#include "../xrsound/sound.h"
//#include "../xrengine/IGame_Level.h"

#pragma comment( lib, "xrCore.lib"	)

#include "xrPhysics.h"

#include "../include/xrapi/xrapi.h"
#ifdef	DEBUG
#include "d3d9types.h"
#endif
//IC IGame_Level &GLevel()
//{
//	VERIFY( g_pGameLevel );
//	return *g_pGameLevel;
//}
class CGameMtlLibrary;
IC CGameMtlLibrary &GMLibrary()
{
	typedef void *(__cdecl *FnGetGMLib)();
	static FnGetGMLib fn = 0;
	if (!fn)
		fn = (FnGetGMLib)GetProcAddress(GetModuleHandle(NULL), "BGFX_GetGMLib");
	R_ASSERT2(fn, "BGFX_GetGMLib export not found");
	CGameMtlLibrary* p = (CGameMtlLibrary*)fn();
	VERIFY(p);
	return *p;
}