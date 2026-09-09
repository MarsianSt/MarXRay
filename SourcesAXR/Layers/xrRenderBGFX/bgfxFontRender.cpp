#include "stdafx.h"
#include "bgfxFontRender.h"
#include "bgfx_capi.h"
#include "bgfxUIProgram.h"
#include "bgfxUIShader.h"
#include "bgfxRenderInterface.h"

#include "../../xrEngine/GameFont.h"
#include "../../xrEngine/MbHelpers.h"

// Font scale. Engine's g_current_font_scale lives in xr_3da (not importable
// here); it is (1,1) in normal UI mode and only differs in PP-scale mode.
static Fvector2 g_bgfx_font_scale = { 1.0f, 1.0f };

bgfxFontRender::bgfxFontRender()
    : m_width(0)
    , m_height(0)
{
    m_texture = BGFX_INVALID_HANDLE;
}

bgfxFontRender::~bgfxFontRender()
{
    // texture owned by the shared texture cache вЂ” do not destroy here
}

void bgfxFontRender::Initialize(LPCSTR cShader, LPCSTR cTexture)
{
    if (!cTexture || !cTexture[0])
        return;

    bgfx_texture_handle_t tex;
    unsigned int w = 0, h = 0;
    if (bgfxLoadUITexture(cTexture, tex, w, h))
    {
        m_texture = tex;
        m_width = w;
        m_height = h;
        LogInfo("[BGFX] FontRender: texture '%s' %ux%u", cTexture, w, h);
    }
    else
        LogError("[BGFX] FontRender: failed to load texture '%s'", cTexture);
}

// Uses FontVertex from bgfxFontRender.h вЂ” same layout as g_uiVertexLayout

static void PackColor(u32 C, u32& out)
{
    // 0xAARRGGBB -> memory RGBA byte order
    out = (C & 0xFF00FF00u) | ((C >> 16) & 0x000000FFu) | ((C << 16) & 0x00FF0000u);
}

void bgfxFontRender::OnRender(CGameFont &owner)
{
    if (owner.strings.empty()) // early exit if there is no text to render
        return;

    if (!bgfxIsValid(m_texture))
        return;

    if (!(owner.uFlags & CGameFont::fsValid))
    {
        owner.vTS.set((int)m_width, (int)m_height);
        owner.fTCHeight = owner.fHeight / float(owner.vTS.y);
        owner.uFlags |= CGameFont::fsValid;
    }

    u32 w = g_bgfxRenderTarget.m_width;
    u32 h = g_bgfxRenderTarget.m_height;
    if (0 == w || 0 == h)
    {
        w = 1024;
        h = 768;
    }

    const float invW = 2.0f / float(w);
    const float invH = 2.0f / float(h);

    bgfx_program_handle_t prog = bgfxFontProgramGet();
    if (!bgfxUIProgramValid(prog))
        return;

    for (u32 i = 0; i < owner.strings.size(); )
    {
        // calculate first-fit
        int count = 1;

        u32 length = owner.smart_strlen(owner.strings[i].string);

        while ((i + count) < owner.strings.size())
        {
            u32 L = owner.smart_strlen(owner.strings[i + count].string);

            if ((L + length) < MAX_MB_CHARS)
            {
                count++;
                length += L;
            }
            else break;
        }

        const u32 last = i + count;

        // Worst case: every char is a glyph
        const u32 maxGlyphs = length * 4;
        xr_vector<FontVertex> verts;
        xr_vector<u16> indices;
        verts.reserve(maxGlyphs);
        indices.reserve(length * 6);

        u32 di = i;

        if (owner.GetFontShadowEnabled())
            RenderFragment(owner, di, true, owner.GetFontShadowX(), owner.GetFontShadowY(), length, last, w, h, invW, invH, verts, indices);

        RenderFragment(owner, i, false, 0, 0, length, last, w, h, invW, invH, verts, indices);

        const u32 vCount = (u32)verts.size();
        if (vCount && !indices.empty())
        {
            bgfx_transient_vertex_buffer_t tvb;
            bgfx_alloc_transient_vertex_buffer(&tvb, vCount, &g_uiVertexLayout);
            if (tvb.data)
                memcpy(tvb.data, &verts[0], vCount * sizeof(FontVertex));

            bgfx_transient_index_buffer_t tib;
            bgfx_alloc_transient_index_buffer(&tib, (u32)indices.size(), false);
            if (tib.data)
                memcpy(tib.data, &indices[0], indices.size() * sizeof(u16));

            if (tvb.data && tib.data)
            {
                bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA, 0);
                bgfx_set_transient_vertex_buffer(0, &tvb, 0, vCount);
                bgfx_set_transient_index_buffer(&tib, 0, (u32)indices.size());
                bgfx_uniform_handle_t sampler = bgfxUITextureSamplerGet();
                bgfx_set_texture(0, sampler, m_texture, UINT32_MAX);
                bgfxUIScissorApply();
                bgfx_submit(0, prog, 0, BGFX_DISCARD_ALL);
            }
        }
    }
}

void bgfxFontRender::RenderFragment(CGameFont& owner, u32& i, bool shadow_mode, float dX, float dY, u32 length, u32 last,
    u32 w, u32 h, float invW, float invH, xr_vector<FontVertex>& verts, xr_vector<u16>& indices)
{
    for (; i < last; i++)
    {
        CGameFont::String& PS = owner.strings[i];
        wide_char wsStr[MAX_MB_CHARS];

        u32 len = owner.IsMultibyte()
            ? mbhMulti2Wide(wsStr, nullptr, MAX_MB_CHARS, PS.string)
            : xr_strlen(PS.string);

        if (len)
        {
            float X = float(iFloor(PS.x)) + dX;
            float Y = float(iFloor(PS.y)) + dY;

            float S = PS.height * g_bgfx_font_scale.y * owner.GetCurrentHeightScale();

            float Y2 = Y + S;
            float fSize = 0;

            if (PS.align)
                fSize = owner.IsMultibyte() ? owner.SizeOf_(wsStr) : owner.SizeOf_(PS.string);

            switch (PS.align)
            {
            case CGameFont::alCenter:
                X -= (iFloor(fSize * 0.5f)) * g_bgfx_font_scale.x;
                break;
            case CGameFont::alRight:
                X -= iFloor(fSize) * g_bgfx_font_scale.x;
                break;
            }

            u32 clr, clr2;
            clr2 = clr = PS.c;
            if (owner.uFlags & CGameFont::fsGradient)
            {
                u32 _R = color_get_R(clr) / 2;
                u32 _G = color_get_G(clr) / 2;
                u32 _B = color_get_B(clr) / 2;
                u32 _A = color_get_A(clr);
                clr2 = color_rgba(_R, _G, _B, _A);
            }

            if (shadow_mode)
            {
                // color_argb(220, 20, 20, 20)
                u32 min_alpha = _min(color_get_A(clr), (u32)220);

                u32 _R = color_get_R(clr);
                u32 _G = color_get_G(clr);
                u32 _B = color_get_B(clr);

                float Yl = 0.299f * _R + 0.587f * _G + 0.114f * _B;

                u32 c = Yl >= 40 ? 20 : 120;
                if (!owner.GetFontShadowForBlackText() && (Yl < 40))
                    min_alpha = 0;

                clr2 = clr = color_argb(min_alpha, c, c, c);
            }

            // No screen-space half-pixel offset: our pixel->clip transform
            // maps pixel centers like D3D9. Align sampling to texel centers
            // instead (D3D9-style half-texel UV offset) to avoid bleeding
            // neighboring glyphs at edges.
            float tu = 0, tv = 0;

            for (u32 j = 0; j < len; j++)
            {
                const Fvector l = owner.IsMultibyte() ? owner.GetCharTC(wsStr[1 + j]) : owner.GetCharTC((u16)(u8)PS.string[j]);

                const float scw = l.z * g_bgfx_font_scale.x * owner.GetCurrentWidthScale();

                const float fTCWidth = l.z / owner.vTS.x;

                if (!fis_zero(l.z))
                {
                    tu = (l.x / owner.vTS.x) + (0.5f / owner.vTS.x);
                    tv = (l.y / owner.vTS.y) + (0.5f / owner.vTS.y);

                    FontVertex v0, v1, v2, v3;
                    PackColor(clr2, v0.color); PackColor(clr, v1.color);
                    PackColor(clr2, v2.color); PackColor(clr, v3.color);

                    // Clip space (y-flipped): v0/v2 bottom (Y2), v1/v3 top (Y)
                    v0.x = X * invW - 1.0f;        v0.y = 1.0f - Y2 * invH; v0.z = 0.0f;
                    v0.u = tu;                     v0.v = tv + owner.fTCHeight;

                    v1.x = v0.x;                   v1.y = 1.0f - Y * invH;  v1.z = 0.0f;
                    v1.u = tu;                     v1.v = tv;

                    v2.x = (X + scw) * invW - 1.0f; v2.y = v0.y;            v2.z = 0.0f;
                    v2.u = tu + fTCWidth;          v2.v = v0.v;

                    v3.x = v2.x;                   v3.y = v1.y;             v3.z = 0.0f;
                    v3.u = v2.u;                   v3.v = v1.v;

                    const u16 base = (u16)verts.size();
                    verts.push_back(v0);
                    verts.push_back(v1);
                    verts.push_back(v2);
                    verts.push_back(v3);

                    indices.push_back(base + 0);
                    indices.push_back(base + 1);
                    indices.push_back(base + 2);
                    indices.push_back(base + 2);
                    indices.push_back(base + 1);
                    indices.push_back(base + 3);
                }
                X += scw * owner.GetInterval().x;
                if (owner.IsMultibyte())
                {
                    if (IsNeedSpaceCharacter(wsStr[1 + j]))
                        X += owner.GetfXStep() * owner.GetInterval().x * owner.GetCurrentWidthScale();
                }
            }
        }
    }
}

