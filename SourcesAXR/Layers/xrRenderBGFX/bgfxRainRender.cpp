#include "stdafx.h"
#include "bgfxRainRender.h"

#include "bgfx_capi.h"
#include "bgfxShaderCompiler.h"
#include "bgfxUIShader.h"

#include "../../xrcdb/xrXRC.h"
#include "../../xrSound/sound.h"
#include "../../xrEngine/pure.h"
#include "../../xrEngine/EngineAPI.h"
#include "../../xrEngine/EventAPI.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/Render.h"
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/device.h"

#include "../../xrEngine/Rain.h"
#include "../../xrEngine/x_ray.h"

#include <vector>

// Rain/snow streaks, ported 1:1 from dxRainRender::Render (dxRainRender.cpp:66).
// Submits into the sky/particle view (kBgfxSkyViewId=2) from
// CEnvironment::RenderLast() -> bgfxRenderEnvironmentFx().
// Splash particles (owner.particle_active + dm\rain.dm) are stage 2 and not
// drawn here yet.
namespace
{
    const bgfx_view_id_t kRainView = 2;
    const u32 kMaxQuadsPerSubmit = 1024;

    struct RainVertex
    {
        Fvector  pos;
        u32      color;
        Fvector2 uv;
    };

    bgfx_program_handle_t s_prog = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_params = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_layout = {};
    bool s_layoutReady = false;

    bgfx_texture_handle_t s_tex = BGFX_INVALID_HANDLE;
    bool s_texTried = false;
    bool s_texWinter = false;

    // Mirrors the DX global current_items (dxRainRender.cpp:7).
    int s_current_items = 0;

    // Engine ARGB (0xAARRGGBB) -> bgfx uint8 vertex color memory order R,G,B,A.
    u32 PackColor(u32 C)
    {
        return (C & 0xFF00FF00u) | ((C >> 16) & 0x000000FFu) | ((C << 16) & 0x00FF0000u);
    }

    bool EnsureProgram()
    {
        if (bgfxIsValid(s_prog))
            return true;

        if (!s_layoutReady)
        {
            bgfx_vertex_layout_begin(&s_layout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_layout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_layout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
            bgfx_vertex_layout_add(&s_layout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_layout);
            s_layoutReady = true;
        }

        std::vector<u8> vsBlob, psBlob;
        if (!bgfxShaderCompileFile("particle_vs.sc", 'v', vsBlob) ||
            !bgfxShaderCompileFile("particle_ps.sc", 'f', psBlob))
        {
            LogError("[BGFX] Rain program build failed");
            return false;
        }
        if (vsBlob.empty() || psBlob.empty())
            return false;

        bgfx_shader_handle_t vsh = bgfx_create_shader(bgfx_copy(vsBlob.data(), (u32)vsBlob.size()));
        bgfx_shader_handle_t fsh = bgfx_create_shader(bgfx_copy(psBlob.data(), (u32)psBlob.size()));
        if (!bgfxIsValid(vsh) || !bgfxIsValid(fsh))
            return false;

        s_prog = bgfx_create_program(vsh, fsh, true);
        if (!bgfxIsValid(s_prog))
        {
            LogError("[BGFX] Rain program build failed");
            return false;
        }

        s_sampler = bgfx_create_uniform("s_base", BGFX_UNIFORM_TYPE_SAMPLER, 1);
        s_params = bgfx_create_uniform("u_particleParams", BGFX_UNIFORM_TYPE_VEC4, 1);
        LogInfo("[BGFX] Rain program created: %u", s_prog.idx);
        return true;
    }

    bgfx_texture_handle_t GetRainTexture()
    {
        if (s_texTried && s_texWinter == !!bWinterMode)
            return s_tex;
        if (s_texTried)
            s_tex = BGFX_INVALID_HANDLE;
        s_texTried = true;
        s_texWinter = !!bWinterMode;

        const char* name = bWinterMode ? "fx\\fx_snow" : "fx\\fx_rain";
        unsigned int w = 0, h = 0;
        if (bgfxLoadUITexture(name, s_tex, w, h) && bgfxIsValid(s_tex))
            LogInfo("[BGFX] Rain texture '%s' %ux%u", name, w, h);
        else
        {
            LogError("[BGFX] Rain texture '%s' load failed", name);
            s_tex = BGFX_INVALID_HANDLE;
        }
        return s_tex;
    }

    void SubmitQuads(const RainVertex* quads, u32 quadCount, bgfx_texture_handle_t tex)
    {
        if (!EnsureProgram() || !bgfxIsValid(tex) || !quadCount)
            return;

        bgfx_set_view_rect(kRainView, 0, 0, (u16)Device.dwWidth, (u16)Device.dwHeight);
        bgfx_set_view_clear(kRainView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(kRainView, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_set_view_transform(kRainView, Device.mView.m, Device.mProject.m);
        bgfx_touch(kRainView);

        const float prm[4] = { 0.f, 0.f, 0.f, 0.f };    // alpha test off
        const u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_DEPTH_TEST_LEQUAL
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        Fmatrix ident;
        ident.identity();

        u32 done = 0;
        while (done < quadCount)
        {
            u32 batch = quadCount - done;
            if (batch > kMaxQuadsPerSubmit)
                batch = kMaxQuadsPerSubmit;
            u32 nV = batch * 4;
            u32 nI = batch * 6;

            bgfx_transient_vertex_buffer_t tvb;
            bgfx_transient_index_buffer_t tib;
            if (!bgfx_alloc_transient_buffers(&tvb, &s_layout, nV, &tib, nI, false))
                return;

            memcpy(tvb.data, quads + done * 4, (size_t)nV * sizeof(RainVertex));

            u16* idx = (u16*)tib.data;
            for (u32 q = 0; q < batch; q++)
            {
                u16 base = (u16)(q * 4);
                idx[q * 6 + 0] = base + 0;
                idx[q * 6 + 1] = base + 1;
                idx[q * 6 + 2] = base + 2;
                idx[q * 6 + 3] = base + 3;
                idx[q * 6 + 4] = base + 2;
                idx[q * 6 + 5] = base + 1;
            }

            bgfx_set_transform(ident.m, 1);
            bgfx_set_state(state, 0);
            bgfx_set_transient_vertex_buffer(0, &tvb, 0, nV);
            bgfx_set_transient_index_buffer(&tib, 0, nI);
            bgfx_set_texture(0, s_sampler, tex, 0);
            bgfx_set_uniform(s_params, prm, 1);
            bgfx_submit(kRainView, s_prog, 0, BGFX_DISCARD_ALL);

            done += batch;
        }
    }
}

bgfxRainRender::bgfxRainRender()
{
    m_dropBounds.P.set(0.f, 0.f, 0.f);
    m_dropBounds.R = 0.1f;
}

void bgfxRainRender::Copy(IRainRender &_in)
{
    m_dropBounds = ((bgfxRainRender*)&_in)->m_dropBounds;
}

void bgfxRainRender::Render(CEffect_Rain &owner)
{
    if (!g_pGamePersistent)
        return;

    CEnvDescriptorMixer* env = g_pGamePersistent->Environment().CurrentEnv;
    float factor = env->rain_density;
    if (factor < EPS_L)
        return;

    float particles_multiplier = bWinterMode ? 2.0f : 1.0f;
    float _drop_speed = 1.0f;

    float wind_direction = 0.0f;
    float wind_power = 0.0f;
    if (env->wind_velocity)
    {
        wind_direction = env->wind_direction;
        wind_power = env->wind_velocity;
    }

    Fvector wind_vector;
    wind_vector.set(_sin(wind_direction), 0.0f, _cos(wind_direction));

    u32 desired_items = iFloor(particles_multiplier * 0.01f * (1.f + factor * 99.0f) * m_imax_desired_items);
    if (s_current_items < (int)desired_items)
        s_current_items = (int)desired_items;

    float factor_visual = factor / 2.f + .5f;
    Fvector3 f_rain_color = env->rain_color;
    u32 u_rain_color = PackColor(color_rgba_f(f_rain_color.x, f_rain_color.y, f_rain_color.z, factor_visual));

    // Born _new_ if needed
    float b_radius_wrap_sqr = _sqr((m_fsource_radius * 1.5f));
    if (owner.items.size() < (size_t)s_current_items)
    {
        while (owner.items.size() < (size_t)s_current_items)
        {
            CEffect_Rain::Item one;
            owner.Born(one, m_fsource_radius, _drop_speed);
            owner.items.push_back(one);
        }
    }

    // Build source plane
    Fplane src_plane;
    Fvector norm = { 0.f, -1.f, 0.f };
    Fvector upper;
    upper.set(Device.vCameraPosition.x, Device.vCameraPosition.y + m_fsource_offset, Device.vCameraPosition.z);
    src_plane.build(upper, norm);

    const Fvector& vEye = Device.vCameraPosition;
    std::vector<RainVertex> quads;
    quads.reserve((size_t)desired_items * 4);

    for (int I = 0; I < s_current_items; I++)
    {
        CEffect_Rain::Item& one = owner.items.at(I);

        if (one.dwTime_Hit < Device.dwTimeGlobal)
        {
            owner.Hit(one.Phit);
            if (s_current_items > (int)desired_items)
                s_current_items--; // Hit something
        }

        if (one.dwTime_Life < Device.dwTimeGlobal)
        {
            owner.Born(one, m_fsource_radius, _drop_speed);
            if (s_current_items > (int)desired_items)
                s_current_items--; // Out of life
        }

        float dt = Device.fTimeDelta;

        if (bWinterMode)
        {
            Fvector wind_effect;
            wind_effect.mul(wind_vector, wind_power * 0.5f * dt);

            Fvector movement;
            movement.set(one.D.x + wind_effect.x, one.D.y, one.D.z + wind_effect.z);
            one.P.mad(movement, one.fSpeed * (0.75f + (wind_power * 0.2f)) * dt);
        }
        else
            one.P.mad(one.D, one.fSpeed * dt);

        Fvector wdir;
        wdir.set(one.P.x - vEye.x, 0, one.P.z - vEye.z);
        float wlen = wdir.square_magnitude();
        if (wlen > b_radius_wrap_sqr)
        {
            wlen = _sqrt(wlen);
            if ((one.P.y - vEye.y) < m_fsink_offset)
            {
                one.invalidate();
            }
            else
            {
                Fvector inv_dir, src_p;
                inv_dir.invert(one.D);
                wdir.div(wlen);
                one.P.mad(one.P, wdir, -(wlen + m_fsource_radius));
                if (src_plane.intersectRayPoint(one.P, inv_dir, src_p))
                {
                    float dist_sqr = one.P.distance_to_sqr(src_p);
                    float height = m_fmax_distance;
                    if (owner.RayPick(src_p, one.D, height, collide::rqtBoth))
                    {
                        if (_sqr(height) <= dist_sqr)
                            one.invalidate();
                        else
                            owner.RenewItem(one, height - _sqrt(dist_sqr), TRUE);
                    }
                    else
                    {
                        owner.RenewItem(one, m_fmax_distance - _sqrt(dist_sqr), FALSE);
                    }
                }
                else
                {
                    one.invalidate();
                }
            }
        }

        // Build line
        Fvector& pos_head = one.P;
        Fvector pos_trail;
        if (!bWinterMode)
            pos_trail.mad(pos_head, one.D, -owner.drop_length * factor_visual);
        else
            pos_trail.mad(pos_head, one.D, -owner.drop_length * 5.5f);

        // Culling
        Fvector sC, lineD;
        float sR;
        sC.sub(pos_head, pos_trail);
        lineD.normalize(sC);
        sC.mul(.5f);
        sR = sC.magnitude();
        sC.add(pos_trail);
        if (!::Render->ViewBase.testSphere_dirty(sC, sR))
            continue;

        static const Fvector2 UV[2][4] = {
            { {0,1}, {0,0}, {1,1}, {1,0} },
            { {1,0}, {1,1}, {0,0}, {0,1} }
        };

        // Everything OK - build vertices
        Fvector lineTop, camDir;
        camDir.sub(sC, vEye);
        camDir.normalize();
        lineTop.crossproduct(camDir, lineD);
        float w = owner.drop_width;
        u32 s = one.uv_set;

        RainVertex v;
        v.color = u_rain_color;

        v.pos.mad(pos_trail, lineTop, -w); v.uv.set(UV[s][0].x, UV[s][0].y); quads.push_back(v);
        v.pos.mad(pos_trail, lineTop, w);  v.uv.set(UV[s][1].x, UV[s][1].y); quads.push_back(v);
        v.pos.mad(pos_head, lineTop, -w);  v.uv.set(UV[s][2].x, UV[s][2].y); quads.push_back(v);
        v.pos.mad(pos_head, lineTop, w);   v.uv.set(UV[s][3].x, UV[s][3].y); quads.push_back(v);
    }

    if (!quads.empty())
        SubmitQuads(quads.data(), (u32)(quads.size() / 4), GetRainTexture());
}

const Fsphere& bgfxRainRender::GetDropBounds() const
{
    return m_dropBounds;
}
