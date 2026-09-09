#pragma once
#include "..\..\Include\xrRender\DebugRender.h"

class bgfxDebugRender : public IDebugRender
{
public:
    virtual void Render() override;
    virtual void add_lines(Fvector const *vertices, u32 const &vertex_count, u16 const *pairs, u32 const &pair_count, u32 const &color, bool hud_mode = false) override;
    virtual void NextSceneMode() override;
    virtual void ZEnable(bool bEnable) override;
    virtual void OnFrameEnd() override;
    virtual void SetShader(const debug_shader &shader) override;
    virtual void CacheSetXformWorld(const Fmatrix& M) override;
    virtual void CacheSetCullMode(CullMode) override;
    virtual void SetAmbient(u32 colour) override;
    virtual void SetDebugShader(dbgShaderHandle shdHandle) override;
    virtual void DestroyDebugShader(dbgShaderHandle shdHandle) override;
    virtual void dbg_DrawTRI(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C) override;
};
