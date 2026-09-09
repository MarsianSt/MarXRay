#include "stdafx.h"
#include "bgfxRenderFactory.h"

#include "bgfxFontRender.h"
#include "bgfxConsoleRender.h"
#include "bgfxDebugRender.h"
#include "bgfxEnvironmentRender.h"
#include "bgfxLensFlareRender.h"
#include "bgfxRainRender.h"
#include "bgfxThunderboltRender.h"
#include "bgfxStatGraphRender.h"
#include "bgfxStatsRender.h"
#include "bgfxWallMarkArray.h"
#include "bgfxUISequenceVideoItem.h"
#include "bgfxUIShader.h"
#include "bgfxRenderDeviceRender.h"
#include "bgfxObjectSpaceRender.h"

bgfxRenderFactory RenderFactoryImpl;

#define RENDER_FACTORY_IMPLEMENT(Class) \
    I##Class* bgfxRenderFactory::Create##Class() \
    { \
        LogDebug("[BGFX] Create##Class()"); \
        return xr_new<bgfx##Class>(); \
    } \
    void bgfxRenderFactory::Destroy##Class(I##Class *pObject) \
    { \
        LogDebug("[BGFX] Destroy##Class()"); \
        xr_delete((bgfx##Class*&)pObject); \
    }

#ifndef _EDITOR
    RENDER_FACTORY_IMPLEMENT(UISequenceVideoItem)
    RENDER_FACTORY_IMPLEMENT(UIShader)
    RENDER_FACTORY_IMPLEMENT(StatGraphRender)
    RENDER_FACTORY_IMPLEMENT(ConsoleRender)
    RENDER_FACTORY_IMPLEMENT(RenderDeviceRender)
#ifdef DEBUG
    RENDER_FACTORY_IMPLEMENT(ObjectSpaceRender)
#endif
    RENDER_FACTORY_IMPLEMENT(WallMarkArray)
    RENDER_FACTORY_IMPLEMENT(StatsRender)
#endif

#ifndef _EDITOR
    RENDER_FACTORY_IMPLEMENT(ThunderboltRender)
    RENDER_FACTORY_IMPLEMENT(ThunderboltDescRender)
    RENDER_FACTORY_IMPLEMENT(RainRender)
    RENDER_FACTORY_IMPLEMENT(LensFlareRender)
    RENDER_FACTORY_IMPLEMENT(EnvironmentRender)
    RENDER_FACTORY_IMPLEMENT(EnvDescriptorMixerRender)
    RENDER_FACTORY_IMPLEMENT(EnvDescriptorRender)
    RENDER_FACTORY_IMPLEMENT(FlareRender)
#endif
RENDER_FACTORY_IMPLEMENT(FontRender)
