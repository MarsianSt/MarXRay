#pragma once
#include "..\..\Include\xrRender\StatGraphRender.h"

class bgfxStatGraphRender : public IStatGraphRender
{
public:
    virtual void Copy(IStatGraphRender &_in) override;
    virtual void OnDeviceCreate() override;
    virtual void OnDeviceDestroy() override;
    virtual void OnRender(CStatGraph &owner) override;
};
