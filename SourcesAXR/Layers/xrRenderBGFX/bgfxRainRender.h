#pragma once
#include "..\..\Include\xrRender\RainRender.h"

class bgfxRainRender : public IRainRender
{
public:
    virtual void Copy(IRainRender &_in) override;
    virtual void Render(CEffect_Rain &owner) override;
    virtual const Fsphere& GetDropBounds() const override;

private:
    Fsphere m_dropBounds;
};
