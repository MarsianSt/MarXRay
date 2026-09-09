#pragma once
#include "..\..\Include\xrRender\LensFlareRender.h"

class bgfxFlareRender : public IFlareRender
{
public:
    virtual void Copy(IFlareRender &_in) override;
    virtual void CreateShader(LPCSTR sh_name, LPCSTR tex_name) override;
    virtual void DestroyShader() override;
};

class bgfxLensFlareRender : public ILensFlareRender
{
public:
    virtual void Copy(ILensFlareRender &_in) override;
    virtual void Render(CLensFlare &owner, BOOL bSun, BOOL bFlares, BOOL bGradient) override;
    virtual void OnDeviceCreate() override;
    virtual void OnDeviceDestroy() override;
};
