#pragma once
#include "..\..\Include\xrRender\WallMarkArray.h"

class bgfxWallMarkArray : public IWallMarkArray
{
public:
    virtual void Copy(IWallMarkArray &_in) override;
    virtual void AppendMark(LPCSTR s_textures) override;
    virtual void clear() override;
    virtual bool empty() override;
    virtual wm_shader GenerateWallmark() override;
};
