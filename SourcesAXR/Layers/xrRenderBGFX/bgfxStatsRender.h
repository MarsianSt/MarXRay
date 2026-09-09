#pragma once
#include "..\..\Include\xrRender\StatsRender.h"

class bgfxStatsRender : public IStatsRender
{
public:
    virtual void Copy(IStatsRender &_in) override;
    virtual void OutData1(CGameFont &F) override;
    virtual void OutData2(CGameFont &F) override;
    virtual void OutData3(CGameFont &F) override;
    virtual void OutData4(CGameFont &F) override;
    virtual void GuardVerts(CGameFont &F) override;
    virtual void GuardDrawCalls(CGameFont &F) override;
    virtual void SetDrawParams(IRenderDeviceRender *pRender) override;
};
