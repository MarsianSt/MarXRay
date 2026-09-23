#include "stdafx.h"
#pragma hdrstop

#include "bgfxParticleRender.h"
#include "ParticleEffectDef.h"
#include "../bgfxShaderCompiler.h"

#include <cmath>
#include <cstring>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace bgfxParticles
{
namespace
{

const u32 kMaxQuadsPerSubmit = 1024;

bgfx_program_handle_t s_prog = BGFX_INVALID_HANDLE;
bgfx_uniform_handle_t s_base = BGFX_INVALID_HANDLE;
bgfx_uniform_handle_t s_params = BGFX_INVALID_HANDLE;
bgfx_vertex_layout_t s_layout = {};
bool s_layoutReady = false;

std::map<std::string, bgfx_texture_handle_t> s_texCache;

void PackColor(u32 C, u32& out)
{
    out = (C & 0xFF00FF00u) | ((C >> 16) & 0x000000FFu) | ((C << 16) & 0x00FF0000u);
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

    std::vector<u8> vsBlob;
    std::vector<u8> psBlob;
    if (!bgfxShaderCompileFile("particle_vs.sc", 'v', vsBlob) ||
        !bgfxShaderCompileFile("particle_ps.sc", 'f', psBlob))
    {
        LogError("[BGFX] Particle program build failed");
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
        LogError("[BGFX] Particle program build failed");
        return false;
    }

    s_base = bgfx_create_uniform("s_base", BGFX_UNIFORM_TYPE_SAMPLER, 1);
    s_params = bgfx_create_uniform("u_particleParams", BGFX_UNIFORM_TYPE_VEC4, 1);
    LogInfo("[BGFX] Particle program created: %u", s_prog.idx);
    return true;
}

void SetupView(const Fmatrix* projOverride)
{
    const Fmatrix& proj = projOverride ? *projOverride : Device.mProject;
    bgfx_set_view_rect(kView, 0, 0, (u16)Device.dwWidth, (u16)Device.dwHeight);
    bgfx_set_view_clear(kView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(kView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_set_view_transform(kView, Device.mView.m, proj.m);
    bgfx_touch(kView);
}

}

void OnDeviceCreate()
{
    EnsureProgram();
}

void OnDeviceDestroy()
{
    if (bgfxIsValid(s_prog)) { bgfx_destroy_program(s_prog); s_prog = BGFX_INVALID_HANDLE; }
    if (bgfxIsValid(s_base)) { bgfx_destroy_uniform(s_base); s_base = BGFX_INVALID_HANDLE; }
    if (bgfxIsValid(s_params)) { bgfx_destroy_uniform(s_params); s_params = BGFX_INVALID_HANDLE; }
    for (auto& e : s_texCache)
        if (bgfxIsValid(e.second))
            bgfx_destroy_texture(e.second);
    s_texCache.clear();
    s_layoutReady = false;
}

bgfx_texture_handle_t GetTexture(LPCSTR name)
{
    if (!name || !name[0])
        return BGFX_INVALID_HANDLE;

    std::string key(name);
    auto it = s_texCache.find(key);
    if (it != s_texCache.end())
        return it->second;

    bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;
    unsigned int w = 0, h = 0;
    if (bgfxLoadWorldTexture(name, tex, w, h))
    {
        LogInfo("[BGFX] Particle '%s' %ux%u h=%u", name, w, h, tex.idx);
        s_texCache[key] = tex;
    }
    else
        LogError("[BGFX] Particle: failed to load '%s'", name);
    return tex;
}

u64 BlendState(int blendMode, bool writeZ)
{
    u64 st = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL;
    if (writeZ)
        st |= BGFX_STATE_WRITE_Z;
    switch (blendMode)
    {
    case BLEND_BLEND:
        st |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        break;
    case BLEND_ADD:
        st |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
        break;
    case BLEND_MUL:
        st |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);
        break;
    case BLEND_MUL_2X:
        st |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_SRC_COLOR);
        break;
    case BLEND_ALPHA_ADD:
        st |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
        break;
    case BLEND_SET:
    default:
        break;
    }
    return st;
}

int BlendFromShaderName(LPCSTR shaderName)
{
    if (!shaderName || !shaderName[0])
        return BLEND_BLEND;

    std::string s(shaderName);
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    const char* n = s.c_str();

    if (strstr(n, "alpha_add") || strstr(n, "aadd"))
        return BLEND_ALPHA_ADD;
    if (strstr(n, "xadd") || strstr(n, "add"))
        return BLEND_ADD;
    if (strstr(n, "mul_2x") || strstr(n, "mul2x"))
        return BLEND_MUL_2X;
    if (strstr(n, "mul"))
        return BLEND_MUL;
    if (strstr(n, "set"))
        return BLEND_SET;
    return BLEND_BLEND;
}

void BuildBillboardQuad(Vertex out[4], const Fvector& center,
    const Fvector& axisT, const Fvector& axisR,
    float r_x, float r_y, float sina, float cosa,
    const Fvector2& lt, const Fvector2& rb, u32 color)
{
    Fvector Vr, Vt, a, b, c, d;
    Vr.set(r_x * (axisT.x * sina + axisR.x * cosa),
           r_x * (axisT.y * sina + axisR.y * cosa),
           r_x * (axisT.z * sina + axisR.z * cosa));
    Vt.set(r_y * (axisT.x * cosa - axisR.x * sina),
           r_y * (axisT.y * cosa - axisR.y * sina),
           r_y * (axisT.z * cosa - axisR.z * sina));
    a.sub(Vt, Vr);
    b.add(Vt, Vr);
    c.set(-a.x, -a.y, -a.z);
    d.set(-b.x, -b.y, -b.z);

    out[0].pos.add(d, center); out[0].color = color; out[0].uv.set(lt.x, rb.y);
    out[1].pos.add(a, center); out[1].color = color; out[1].uv.set(lt.x, lt.y);
    out[2].pos.add(c, center); out[2].color = color; out[2].uv.set(rb.x, rb.y);
    out[3].pos.add(b, center); out[3].color = color; out[3].uv.set(rb.x, lt.y);
}

void BuildCameraBillboard(Vertex out[4], const Fvector& center,
    float r_x, float r_y, float sina, float cosa,
    const Fvector2& lt, const Fvector2& rb, u32 color)
{
    BuildBillboardQuad(out, center, Device.vCameraTop, Device.vCameraRight,
        r_x, r_y, sina, cosa, lt, rb, color);
}

bool Submit(bgfx_texture_handle_t tex, const Vertex* quads, u32 quadCount,
    int blendMode, bool writeZ, bool alphaTest, u8 alphaRef,
    const Fmatrix* projOverride)
{
    if (!EnsureProgram() || !bgfxIsValid(tex) || !quads || !quadCount)
        return false;

    SetupView(projOverride);

    float prm[4] = { (float)alphaRef, alphaTest ? 1.f : 0.f, 0.f, 0.f };

    Fmatrix ident;
    ident.identity();
    u64 state = BlendState(blendMode, writeZ);

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
            return done > 0;

        memcpy(tvb.data, quads + done * 4, (size_t)nV * sizeof(Vertex));

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
        bgfx_set_texture(0, s_base, tex, 0);
        bgfx_set_uniform(s_params, prm, 1);
        bgfx_submit(kView, s_prog, 0, BGFX_DISCARD_ALL);

        done += batch;
    }
    return true;
}

bool SubmitPAPI(PAPI::Particle* particles, u32 count, PS::CPEDef* def,
    int blendMode, const Fmatrix* xformOrNull, const Fmatrix* projOverride)
{
    if (!particles || !count || !def || !def->m_Flags.is(PS::CPEDef::dfSprite))
        return false;

    bgfx_texture_handle_t tex = GetTexture(def->m_TextureName.c_str());
    if (!bgfxIsValid(tex))
        return false;

    bool framed = def->m_Flags.is(PS::CPEDef::dfFramed);
    bool velScale = def->m_Flags.is(PS::CPEDef::dfVelocityScale);

    std::vector<Vertex> quads;
    quads.resize((size_t)count * 4);

    for (u32 i = 0; i < count; i++)
    {
        PAPI::Particle& m = particles[i];

        Fvector2 lt, rb;
        lt.set(0.f, 0.f);
        rb.set(1.f, 1.f);
        if (framed)
            def->m_Frame.CalculateTC(iFloor(float(m.frame) / 255.f), lt, rb);

        float sina = std::sinf(m.rot.x);
        float cosa = std::cosf(m.rot.x);

        float r_x = m.size.x * 0.5f;
        float r_y = m.size.y * 0.5f;
        if (velScale)
        {
            float speed = std::sqrt(m.vel.x * m.vel.x + m.vel.y * m.vel.y + m.vel.z * m.vel.z);
            r_x += speed * def->m_VelocityScale.x;
            r_y += speed * def->m_VelocityScale.y;
        }

        Fvector center;
        center.set(m.pos.x, m.pos.y, m.pos.z);
        if (xformOrNull)
        {
            Fvector src = center;
            xformOrNull->transform_tiny(center, src);
        }

        u32 color;
        PackColor(m.color, color);

        Vertex* out = &quads[(size_t)i * 4];
        BuildCameraBillboard(out, center, r_x, r_y, sina, cosa, lt, rb, color);
    }

    bool isSet = (blendMode == BLEND_SET);
    return Submit(tex, quads.data(), count, blendMode,
        isSet, isSet, 200, projOverride);
}

}
