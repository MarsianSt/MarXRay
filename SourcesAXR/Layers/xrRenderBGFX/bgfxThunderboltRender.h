#pragma once
#include "..\..\Include\xrRender\ThunderboltRender.h"
#include "..\..\Include\xrRender\ThunderboltDescRender.h"

class bgfxThunderboltRender : public IThunderboltRender
{
public:
    virtual void Copy(IThunderboltRender &_in) override;
    virtual void Render(CEffect_Thunderbolt &owner) override;
};

class bgfxThunderboltDescRender : public IThunderboltDescRender
{
public:
    virtual void Copy(IThunderboltDescRender &_in) override;
    virtual void CreateModel(LPCSTR m_name) override;
    virtual void DestroyModel() override;
};
