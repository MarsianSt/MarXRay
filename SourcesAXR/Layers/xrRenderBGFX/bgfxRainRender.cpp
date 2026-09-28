#include "stdafx.h"
#include "bgfxRainRender.h"

#include "bgfx_capi.h"
#include "bgfxShaderCompiler.h"
#include "bgfxUIShader.h"
#include "port/bgfxHDR.h"

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

// Rain / snow streaks, ported 1:1 from dxRainRender::Render
// (archive_sourse\Layers\xrRender\dxRainRender.cpp:66-351).
//
// This is a FORWARD pass, not a G-buffer writer. The reference draws the streaks
// into rt_Color - the already-composed, tone-mapped LDR image - after the
// combine and before the forward phase:
//
//   r4_rendertarget_phase_combine.cpp:355-366
//     // Water rendering & Rain/thunder-bolts
//     {
//        if (!RImplementation.o.dx10_msaa)
//            u_setrt(rt_Generic_0, 0, 0, HW.pBaseZB);
//        ...
//        g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
//     }
//
// reached through CEnvironment::RenderLast (Environment_render.cpp:207-215,
// eff_Rain->Render). bgfxRenderEnvironmentFx() calls the same entry point, and
// the view it submits into is bgfxHDR::kForwardView, the port's slot for exactly
// this phase.
//
// The previous revision of this file reused particle_vs.sc / particle_ps.sc and
// submitted into kSceneFxView, i.e. it wrote the three G-buffer attachments
// (position, packed normal+hemi, albedo) and the streaks were then resolved,
// fogged and tone-mapped as if they were level geometry.
namespace
{
    const u32 kMaxQuadsPerSubmit = 1024;

    // dxRainRender.cpp:96, :221, :250, :281 gate the whole screen-space-shaders
    // arm on ps_r4_shaders_flags.test(R4FLAG_SSS_ADDON). That variable is defined
    // in the xrRender module (Layers/xrRender/xrRender_console.cpp:406), which is
    // not part of this solution - only xrRenderBGFX is - so it cannot be linked
    // from here, exactly like ps_r2_static_flags (see the same reasoning in
    // bgfxRenderInterface.h:166-179). Its reference initial value is
    // { R4FLAG_ES_ADDON }, i.e. R4FLAG_SSS_ADDON (1 << 0) is CLEAR, and its only
    // writer, the "r4_screen_space_shaders" console command
    // (xrRender_console.cpp:1365), lives in that same unbuilt module. Every
    // process that runs this port therefore evaluates dxRainRender.cpp:96 to the
    // same FALSE the reference does with the shipped configuration, so the
    // screen-space arm is a constant false here and the stock arm - owner
    // drop_length / drop_width, speed 1.0, the stub_default streak shader - is
    // the one that runs.
    const bool kSssRainAddon = false;   // R4FLAG_SSS_ADDON, xrRender_console.cpp:406

    struct RainVertex
    {
        Fvector  pos;
        u32      color;
        Fvector2 uv;
    };

    bgfx_program_handle_t s_prog = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sampler = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_setup = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_layout = {};
    bool s_layoutReady = false;

    bgfx_texture_handle_t s_tex = BGFX_INVALID_HANDLE;
    bool s_texTried = false;
    bool s_texWinter = false;

    // Mirrors the DX global current_items (dxRainRender.cpp:7).
    int s_current_items = 0;

    // Engine ARGB (0xAARRGGBB) -> bgfx uint8 vertex color memory order R,G,B,A.
    // This is the reference's `I.Color.bgra` swizzle (stub_default.vs:12),
    // applied here because bgfx declares COLOR0 as UINT8x4 normalised, i.e.
    // already in the order the shader reads it.
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
        if (!bgfxShaderCompileFile("rain_vs.sc", 'v', vsBlob) ||
            !bgfxShaderCompileFile("rain_ps.sc", 'f', psBlob))
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
        s_setup = bgfx_create_uniform("ssfx_rain_setup", BGFX_UNIFORM_TYPE_VEC4, 1);
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

        // dxRainRender.cpp:26 "effects\rain" + "fx\fx_rain"; :46 "effects\snow"
        // + "fx\fx_snow". effects_rain.s:8 binds t_base to s_base, so the texture
        // name is the second create() argument.
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

    void SubmitQuads(const RainVertex* quads, u32 quadCount, bgfx_texture_handle_t tex,
        const float (&setup)[4])
    {
        if (!EnsureProgram() || !bgfxIsValid(tex) || !quadCount)
            return;

        // kRainView, not kForwardView: the reference draws the rain
        // (r4_rendertarget_phase_combine.cpp:365, Environment().RenderLast) into
        // rt_Generic_0 BEFORE the forward phase (:386 RImplementation.render_forward),
        // so the streaks have to be a separate view placed one slot earlier. Sharing
        // kForwardView with the particles would put both passes in one bgfx sort
        // bucket, where the draws are co-sorted instead of run in reference order.
        const bgfx_view_id_t view = bgfxHDR::kRainView;
        bgfx_set_view_rect(view, 0, 0, (u16)Device.dwWidth, (u16)Device.dwHeight);
        bgfx_set_view_clear(view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx_set_view_mode(view, BGFX_VIEW_MODE_SEQUENTIAL);
        bgfx_set_view_transform(view, Device.mView.m, Device.mProject.m);
        // The reference binds rt_Generic_0 (the combine output) with the scene
        // depth still attached (r4_rendertarget_phase_combine.cpp:358), so the
        // streaks depth-test against the level. GetForwardFrameBuffer() is exactly
        // that pair: s_smaaInput + s_hdrDepth. It is only built once the SMAA
        // targets exist, so before that the pass has no target and draws nothing -
        // the same guard every other pass in this module uses.
        const bgfx_frame_buffer_handle_t fb = bgfxHDR::GetForwardFrameBuffer();
        if (!bgfxIsValid(fb))
        {
            static bool s_warned = false;
            if (!s_warned)
            {
                s_warned = true;
                LogError("[BGFX] Rain: no forward frame buffer, streaks skipped");
            }
            return;
        }
        bgfx_set_view_frame_buffer(view, fb);
        bgfx_touch(view);

        // effects_rain.s:3-6 - zb(true,false) + blend(srcalpha, invsrcalpha) +
        // aref(true,0), applied by dxRainRender.cpp:264-269. zb(true,false) is
        // z-test on / z-write off, i.e. DEPTH_TEST_LEQUAL with no WRITE_Z.
        // aref(true,0) discards nothing: the alpha reference is zero, so every
        // fragment passes and no clip() is emitted (stub_default.ps:9-12 has the
        // clip calls commented out for exactly this shader).
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
            // dxRainRender.cpp:81,270 - static shared_str s_shader_setup =
            // "ssfx_rain_setup", set right after the streak draw with
            // ps_ssfx_rain_2 (alpha, brightness, refraction, reflection).
            bgfx_set_uniform(s_setup, setup, 1);
            bgfx_submit(view, s_prog, 0, BGFX_DISCARD_ALL);

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

    // dxRainRender.cpp:77-79, then :96-103. The R4 screen-space-shaders arm
    // replaces the engine's own drop_length / drop_width / speed 1.0 with
    // ps_ssfx_rain_1.x / .y / .z, and swaps the splash shader in; the stock arm
    // keeps owner.drop_length / owner.drop_width and speed 1.0. The arm is
    // R4FLAG_SSS_ADDON and never winter (bWinterMode is excluded at :96).
    float _drop_len = owner.drop_length;
    float _drop_width = owner.drop_width;
    float _drop_speed = 1.0f;
    const bool sss_rain = kSssRainAddon && !bWinterMode;
    if (sss_rain)
    {
        _drop_len = ps_ssfx_rain_1.x;
        _drop_width = ps_ssfx_rain_1.y;
        _drop_speed = ps_ssfx_rain_1.z;
    }

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

        // Build line - dxRainRender.cpp:219-227.
        Fvector& pos_head = one.P;
        Fvector pos_trail;
        if (!bWinterMode)
        {
            if (sss_rain)
                pos_trail.mad(pos_head, one.D, -_drop_len * factor_visual);
            else
                pos_trail.mad(pos_head, one.D, -owner.drop_length * factor_visual);
        }
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
        // dxRainRender.cpp:250 - the screen-space arm also widens the streak.
        float w = sss_rain ? _drop_width : owner.drop_width;
        u32 s = one.uv_set;

        RainVertex v;
        v.color = u_rain_color;

        v.pos.mad(pos_trail, lineTop, -w); v.uv.set(UV[s][0].x, UV[s][0].y); quads.push_back(v);
        v.pos.mad(pos_trail, lineTop, w);  v.uv.set(UV[s][1].x, UV[s][1].y); quads.push_back(v);
        v.pos.mad(pos_head, lineTop, -w);  v.uv.set(UV[s][2].x, UV[s][2].y); quads.push_back(v);
        v.pos.mad(pos_head, lineTop, w);   v.uv.set(UV[s][3].x, UV[s][3].y); quads.push_back(v);
    }

    if (!quads.empty())
    {
        // ps_ssfx_rain_2 (xr_ioc_cmd.cpp:493) = { 0.7f, 0.1f, 1.0f, 0.5f }
        // (alpha, brightness, refraction, reflection), dxRainRender.cpp:270.
        const float setup[4] = { ps_ssfx_rain_2.x, ps_ssfx_rain_2.y, ps_ssfx_rain_2.z, ps_ssfx_rain_2.w };
        SubmitQuads(quads.data(), (u32)(quads.size() / 4), GetRainTexture(), setup);
    }
}

const Fsphere& bgfxRainRender::GetDropBounds() const
{
    return m_dropBounds;
}
