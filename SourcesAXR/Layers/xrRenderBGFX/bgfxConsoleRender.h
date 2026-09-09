#pragma once
#include "..\..\Include\xrRender\ConsoleRender.h"

class bgfxConsoleRender : public IConsoleRender
{
public:
    virtual void Copy(IConsoleRender &_in) override;
    virtual void OnRender(bool bGame) override;
};
