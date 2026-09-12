#pragma once

// Define module name for async logger
#define LOG_MODULE "BGFX"

#ifndef ENGINE_API
#define ENGINE_API __declspec(dllimport)
#endif
#ifndef ECORE_API
#define ECORE_API
#endif
#ifndef DLL_API
#define DLL_API
#endif

// Include engine headers (C++17)
#include "../../xrCore/xrCore.h"

#include "..\..\Include\xrRender\RenderFactory.h"
#include "..\..\Include\xrRender\RenderDeviceRender.h"
#include "..\..\Include\xrRender\FontRender.h"
#include "..\..\Include\xrRender\UIShader.h"
#include "..\..\Include\xrRender\ConsoleRender.h"
#include "..\..\Include\xrRender\DebugRender.h"
#include "..\..\Include\xrRender\EnvironmentRender.h"
#include "..\..\Include\xrRender\particles_systems_library_interface.hpp"
#include "..\..\Include\xrRender\LensFlareRender.h"
#include "..\..\Include\xrRender\RainRender.h"
#include "..\..\Include\xrRender\ThunderboltRender.h"
#include "..\..\Include\xrRender\ThunderboltDescRender.h"
#include "..\..\Include\xrRender\StatGraphRender.h"
#include "..\..\Include\xrRender\StatsRender.h"
#include "..\..\Include\xrRender\WallMarkArray.h"
#include "..\..\Include\xrRender\UISequenceVideoItem.h"
#include "..\..\Include\xrRender\ObjectSpaceRender.h"

#include "../../Include/xrAPI/xrAPI.h"
