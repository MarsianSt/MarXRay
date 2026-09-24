#include "stdafx.h"
#include "import_ses.hpp"

void import_ses::LuaLog(const char* caMessage)
{
	LogInfo("![Script]: %s", caMessage);
}

LUACORE const char* import_ses::user_name() 
{ 
	return (Core.UserName); 
}