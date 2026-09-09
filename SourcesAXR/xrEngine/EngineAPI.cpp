// EngineAPI.cpp: implementation of the CEngineAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EngineAPI.h"
#include "../xrcdb/xrXRC.h"
#include "../../xrEngine/XR_IOConsole.h"

extern xr_vector<xr_token> vid_quality_token;

constexpr const char* bgfx_name = "xrRenderBGFX";

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void __cdecl dummy		(void)	{
};
CEngineAPI::CEngineAPI	()
{
	hGame			= 0;
	hRender			= 0;
	hTuner			= 0;
	pCreate			= 0;
	pDestroy		= 0;
	tune_pause		= dummy	;
	tune_resume		= dummy	;
}

CEngineAPI::~CEngineAPI()
{
	vid_quality_token.clear();
}

extern u32 renderer_value; //con cmd
ENGINE_API int g_current_renderer = 0;

ENGINE_API bool is_enough_address_space_available	()
{
	SYSTEM_INFO		system_info;
	GetSystemInfo	( &system_info );
	return			(*(u32*)&system_info.lpMaximumApplicationAddress) > 0x90000000;	
}

void CEngineAPI::InitializeRenderer()
{
	LogInfo("%s", "Loading DLL:", bgfx_name);
	hRender = LoadLibrary(bgfx_name);
	if (0 == hRender)
		LogInfo("! ...Failed - incompatible hardware.");
	else
		g_current_renderer = renderer_value;
}

void CEngineAPI::Initialize(void)
{
	//////////////////////////////////////////////////////////////////////////
	// render
	renderer_value = 6; // force bgfx
	InitializeRenderer();

	if (0 == hRender)
		R_CHK(GetLastError());

	R_ASSERT2(hRender, "Can't load renderer");

	Device.ConnectToRender();

	// game	
	{
		LPCSTR			g_name = "xrGame.dll";
		LogInfo("%s", "Loading DLL:",g_name);
		hGame			= LoadLibrary	(g_name);
		if (0==hGame)	R_CHK			(GetLastError());
		R_ASSERT2		(hGame,"Game DLL raised exception during loading or there is no game DLL at all");
		pCreate			= (Factory_Create*)		GetProcAddress(hGame,"xrFactory_Create"		);	R_ASSERT(pCreate);
		pDestroy		= (Factory_Destroy*)	GetProcAddress(hGame,"xrFactory_Destroy"	);	R_ASSERT(pDestroy);
	}

	//////////////////////////////////////////////////////////////////////////
	// vTune
	tune_enabled		= FALSE;
	if (strstr(Core.Params,"-tune"))	{
		LPCSTR			g_name	= "vTuneAPI.dll";
		LogInfo("%s", "Loading DLL:",g_name);
		hTuner			= LoadLibrary	(g_name);
		if (0==hTuner)	R_CHK			(GetLastError());
		R_ASSERT2		(hTuner,"Intel vTune is not installed");
		tune_enabled	= TRUE;
		tune_pause		= (VTPause*)	GetProcAddress(hTuner,"VTPause"		);	R_ASSERT(tune_pause);
		tune_resume		= (VTResume*)	GetProcAddress(hTuner,"VTResume"	);	R_ASSERT(tune_resume);
	}
}

void CEngineAPI::Destroy	(void)
{
	if (hGame)				{ FreeLibrary(hGame);	hGame	= 0; }
	if (hRender)			{ FreeLibrary(hRender); hRender = 0; }
	pCreate					= 0;
	pDestroy				= 0;
	Engine.Event._destroy	();
	XRC.r_clear_compact		();
}

void CEngineAPI::SwitchRenderer()
{
	// bgfx-only build, no switching
	LogInfo("Renderer switching not supported in bgfx-only build");
}

extern "C" {
	typedef bool _declspec(dllexport) SupportsVulkanRendering();
};

void CEngineAPI::CreateRendererList()
{
	if (!vid_quality_token.empty())
		return;

	ZoneScoped;

	xr_vector<xr_token> modes;

	// try to initialize BGFX
	LogInfo("%s", "Loading DLL:", bgfx_name);
	SetErrorMode(SEM_FAILCRITICALERRORS);
	hRender = LoadLibrary(bgfx_name);
	SetErrorMode(0);
	if (hRender)
	{
		SupportsVulkanRendering *test_vk_rendering = (SupportsVulkanRendering*)GetProcAddress(hRender, "SupportsVulkanRendering");
		if (test_vk_rendering && test_vk_rendering())
			modes.emplace_back(xr_token("renderer_bgfx", 6));
		FreeLibrary(hRender);
	}

	hRender = nullptr;

	modes.emplace_back(xr_token(nullptr, -1));

	LogInfo(R"(Available render modes[%d]:)", modes.size());
	for (auto& mode : modes)
		if (mode.name)
			LogInfo("%s", mode.name);

	vid_quality_token = std::move(modes);
}
