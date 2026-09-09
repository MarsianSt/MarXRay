#pragma once
#include "..\..\Include\xrRender\ObjectSpaceRender.h"

#ifdef DEBUG

class bgfxObjectSpaceRender : public IObjectSpaceRender
{
public:
    virtual void Copy(IObjectSpaceRender &_in) override;
    virtual void dbgRender() override;
    virtual void dbgAddSphere(const Fsphere &sphere, u32 colour) override;
    virtual void SetShader() override;
};

#endif // DEBUG
