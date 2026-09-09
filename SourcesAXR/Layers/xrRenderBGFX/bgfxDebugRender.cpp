#include "stdafx.h"
#include "bgfxDebugRender.h"

void bgfxDebugRender::Render()
{
}

void bgfxDebugRender::add_lines(Fvector const *vertices, u32 const &vertex_count, u16 const *pairs, u32 const &pair_count, u32 const &color, bool hud_mode)
{
}

void bgfxDebugRender::NextSceneMode()
{
}

void bgfxDebugRender::ZEnable(bool bEnable)
{
}

void bgfxDebugRender::OnFrameEnd()
{
}

void bgfxDebugRender::SetShader(const debug_shader &shader)
{
}

void bgfxDebugRender::CacheSetXformWorld(const Fmatrix& M)
{
}

void bgfxDebugRender::CacheSetCullMode(CullMode)
{
}

void bgfxDebugRender::SetAmbient(u32 colour)
{
}

void bgfxDebugRender::SetDebugShader(dbgShaderHandle shdHandle)
{
}

void bgfxDebugRender::DestroyDebugShader(dbgShaderHandle shdHandle)
{
}

void bgfxDebugRender::dbg_DrawTRI(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C)
{
}
