#include "stdafx.h"
#include "bgfxRainRender.h"

void bgfxRainRender::Copy(IRainRender &_in)
{
    m_dropBounds = ((bgfxRainRender*)&_in)->m_dropBounds;
}

void bgfxRainRender::Render(CEffect_Rain &owner)
{
}

const Fsphere& bgfxRainRender::GetDropBounds() const
{
    return m_dropBounds;
}
