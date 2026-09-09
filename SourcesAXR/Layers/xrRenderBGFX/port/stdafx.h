#pragma once

// Bridge so that ported xrRender files (which do `#include "stdafx.h"`)
// pull in the BGFX project's existing stdafx + our D3D-compat layer.

// In the original engine these come from xrEngine/stdafx.h; here we mimic the
// renderer-DLL build: imports from the engine exe (AdvancedXRay.exe).
#ifndef ENGINE_API
#define ENGINE_API __declspec(dllimport)
#endif
#ifndef ECORE_API
#define ECORE_API
#endif
#ifndef DLL_API
#define DLL_API
#endif

// _RELEASE normally comes from xrEngine/defines.h
#ifndef _RELEASE
#define _RELEASE(x) { if(x) { (x)->Release(); (x)=NULL; } }
#endif

// RENDER path macros (normally defined by the renderer's stdafx; see
// archive_renders_R1_R2/xrRenderPC_*/stdafx.h). We run the modern R4 path.
#ifndef R_R1
#define R_R1 1
#define R_R2 2
#define R_R3 3
#define R_R4 4
#define RENDER R_R4
#endif

#include "../stdafx.h"

// Engine-wide declaration blocks pulled by IGame_Persistent.h/Environment.h
// (DLL_Pure, ref_sound, IEventReceiver). These are dependencies that the
// original code gets from xrEngine/stdafx.h without needing <d3d9.h>.
#include "../../../xrEngine/EngineAPI.h"   // DLL_Pure
#include "../../../xrcdb/xrXRC.h"          // CDB::MODEL used by xrSound/sound.h
#include "../../../xrSound/sound.h"        // ref_sound / ref_sound_data_ptr
#include "../../../xrEngine/EventAPI.h"    // IEventReceiver
#include "../../../xrParticles/psystem.h"  // PAPI::Particle + particle callbacks

#include "xrD3DDefs.h"
#include "R_Backend.h"
#include "FVF.h"

// RDEVICE normally comes from xrEngine/device.h (pulls in the real
// CRenderDevice declaration). Must come AFTER ../stdafx.h so that _BCL
// (from xrCore.h) is already defined when pure.h is processed.
#include "../../../xrEngine/device.h"