#pragma once
#include "..\..\Include\xrRender\FontRender.h"
#include "bgfx_capi.h"

struct FontVertex
{
    float x, y, z;
    u32 color;
    float u, v;
};

class bgfxFontRender : public IFontRender
{
public:
    bgfxFontRender();
    virtual ~bgfxFontRender();

    virtual void Initialize(LPCSTR cShader, LPCSTR cTexture) override;
    virtual void OnRender(CGameFont &owner) override;

private:
    void RenderFragment(CGameFont& owner, u32& i, bool shadow_mode, float dX, float dY, u32 length, u32 last,
        u32 w, u32 h, float invW, float invH, xr_vector<FontVertex>& verts, xr_vector<u16>& indices);

    bgfx_texture_handle_t m_texture;
    unsigned int m_width;
    unsigned int m_height;
};
