#pragma once

#include "../bgfx_capi.h"

namespace bgfxHDR
{
    const bgfx_view_id_t kSceneView = 0;
    const bgfx_view_id_t kCombineView = 2;
    const bgfx_view_id_t kSceneFxView = 6;

    bool CreateHDRTarget(uint16_t _width, uint16_t _height);
    void DestroyHDRTarget();
    bool RecreateOnResize(uint16_t _width, uint16_t _height);
    bool IsReady();
    bool BindScene();
    bool CombinePass(uint16_t _width, uint16_t _height);
}
