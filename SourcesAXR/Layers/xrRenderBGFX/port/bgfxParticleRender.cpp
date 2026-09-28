#include "stdafx.h"
#pragma hdrstop

#include "bgfxParticleRender.h"
#include "ParticleEffectDef.h"
#include "../bgfxShaderCompiler.h"

#include "../../../xrEngine/Environment.h"
#include "../../../xrEngine/IGame_Persistent.h"
#include "../../../xrEngine/device.h"

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

// The deferred particle program, particle_vs.sc / particle_ps.sc: the AXR
// deffer_particle pair, i.e. CBlender_Particle's oBlend==0 (SET) branch
// (Blender_Particle.cpp:126). One G-buffer writer, three MRTs, drawn into the
// scene FX view with the scene frame buffer.
bgfx_program_handle_t s_prog = BGFX_INVALID_HANDLE;
// The two forward programs, particle_fwd_vs.sc shared by particle_fwd_ps.sc
// (particle.ps) and particle_fwd_add_ps.sc (particle_add.ps). Single SV_Target,
// drawn into the forward view on the combine result.
bgfx_program_handle_t s_progFwd    = BGFX_INVALID_HANDLE;
bgfx_program_handle_t s_progFwdAdd = BGFX_INVALID_HANDLE;

bgfx_uniform_handle_t s_base = BGFX_INVALID_HANDLE;
bgfx_uniform_handle_t s_params = BGFX_INVALID_HANDLE;
// calc_fogging's fog_plane, the per-vertex forward fog. Bound by the frame's
// writer below, not by the HDR module: cl_fog_plane
// (Blender_Recorder_StandartBinding.cpp:138-162) is a plain function of
// Device.mFullTransform and CEnvDescriptor::fog_near/fog_far, the two fields
// bgfxHDR's SetEnvironmentUniforms already reads for u_fogParams
// (bgfxHDR.cpp:1776-1789).
bgfx_uniform_handle_t s_fogPlane = BGFX_INVALID_HANDLE;

bgfx_vertex_layout_t s_layout = {};
bool s_layoutReady = false;

std::map<std::string, bgfx_texture_handle_t> s_texCache;

// One queued forward effect. The queue spans the gap between the scene pass and
// the forward pass, because bgfx fixes the order by view id and the reference
// fixes it by the phase it draws in (r4_rendertarget_phase_combine.cpp:374-388).
struct ForwardEffect
{
    std::vector<Vertex>  quads;
    bgfx_texture_handle_t tex        = BGFX_INVALID_HANDLE;
    int                  blendMode   = BLEND_BLEND;
    ForwardPixel         pixel       = FWD_PIXEL_BLEND;
    Fmatrix              proj;
};

std::vector<ForwardEffect> s_forward;
bgfx_frame_buffer_handle_t s_forwardFb = BGFX_INVALID_HANDLE;
// The frame the queue belongs to. RenderForwardPass drains it every frame, but
// a frame that never reaches the pass (the target not bound, the pass not
// called) must not carry its quads into the next one, so the queue drops them
// itself when the frame turns over.
u32 s_forwardFrame = 0xFFFFFFFFu;

void PackColor(u32 C, u32& out)
{
    out = (C & 0xFF00FF00u) | ((C >> 16) & 0x000000FFu) | ((C << 16) & 0x00FF0000u);
}

// CResourceManager::_ParseList (archive_sourse\Layers\xrRender\ResourceManager.cpp:93-128)
// splits a shader texture list on ',', lowercases every element and runs
// fix_texture_name on it. CBlender_Particle::Compile binds only the first
// element to s_base (archive_sourse\Layers\xrRender\Blender_Particle.cpp:135 for
// DX10, :90 for R2), so the tail of the list is parsed but never sampled.
const char* FirstListElementEnd(const char* p)
{
    const char* comma = strchr(p, ',');
    return comma ? comma : p + strlen(p);
}

// fix_texture_name (archive_sourse\Layers\xrRender\Texture.cpp:19-28) drops only
// these four extensions; every other dotted suffix is left alone.
void FixTextureName(std::string& name)
{
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size())
        return;
    std::string ext = name.substr(dot + 1);
    for (char& c : ext)
        c = (char)std::tolower((unsigned char)c);
    if (ext == "tga" || ext == "dds" || ext == "bmp" || ext == "ogm")
        name.erase(dot);
}

// The name the reference hands to CResourceManager::_CreateTexture
// (archive_sourse\Layers\xrRender\ResourceManager_Resources.cpp:418-420): one
// lowercased element, no extension. The VFS loader appends ".dds"/".tga" itself,
// so an extension left on the name would be looked up as "<name>.bmp.dds".
void NormalizeTextureName(LPCSTR in, std::string& out)
{
    out.clear();
    if (!in)
        return;
    for (const char* p = in; p != FirstListElementEnd(in); ++p)
        out.push_back((char)std::tolower((unsigned char)*p));
    FixTextureName(out);
}

bool EnsureLayout()
{
    if (s_layoutReady)
        return true;
    bgfx_vertex_layout_begin(&s_layout, bgfx_get_renderer_type());
    bgfx_vertex_layout_add(&s_layout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
    bgfx_vertex_layout_add(&s_layout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
    bgfx_vertex_layout_add(&s_layout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
    bgfx_vertex_layout_end(&s_layout);
    s_layoutReady = true;
    return true;
}

// Compiles one .sc pair into a program. The reference builds the same pair per
// blend mode through CBlender_Compile (Blender_Particle.cpp:126-131), but the
// blend state is fixed-function, so one program per pixel stage is all the
// permutations collapse to.
bgfx_program_handle_t BuildProgram(const char* vsName, const char* psName)
{
    std::vector<u8> vsBlob;
    std::vector<u8> psBlob;
    if (!bgfxShaderCompileFile(vsName, 'v', vsBlob) ||
        !bgfxShaderCompileFile(psName, 'f', psBlob))
    {
        LogError("[BGFX] Particle program build failed: %s + %s", vsName, psName);
        return BGFX_INVALID_HANDLE;
    }
    if (vsBlob.empty() || psBlob.empty())
        return BGFX_INVALID_HANDLE;

    bgfx_shader_handle_t vsh = bgfx_create_shader(bgfx_copy(vsBlob.data(), (u32)vsBlob.size()));
    bgfx_shader_handle_t fsh = bgfx_create_shader(bgfx_copy(psBlob.data(), (u32)psBlob.size()));
    if (!bgfxIsValid(vsh) || !bgfxIsValid(fsh))
        return BGFX_INVALID_HANDLE;

    bgfx_program_handle_t prog = bgfx_create_program(vsh, fsh, true);
    if (!bgfxIsValid(prog))
    {
        LogError("[BGFX] Particle program build failed: %s + %s", vsName, psName);
        return BGFX_INVALID_HANDLE;
    }
    return prog;
}

// s_base, u_particleParams and u_fogPlane are shared by the deferred and the
// forward programs, so they are created once, by whichever route runs first.
bool EnsureUniforms()
{
    if (bgfxIsValid(s_base))
        return true;
    s_base     = bgfx_create_uniform("s_base", BGFX_UNIFORM_TYPE_SAMPLER, 1);
    s_params   = bgfx_create_uniform("u_particleParams", BGFX_UNIFORM_TYPE_VEC4, 1);
    s_fogPlane = bgfx_create_uniform("u_fogPlane", BGFX_UNIFORM_TYPE_VEC4, 1);
    return true;
}

bool EnsureForwardPrograms();

bool EnsureProgram()
{
    if (bgfxIsValid(s_prog))
        return true;

    EnsureLayout();
    EnsureUniforms();
    s_prog = BuildProgram("particle_vs.sc", "particle_ps.sc");
    if (!bgfxIsValid(s_prog))
        return false;

    LogInfo("[BGFX] Particle program created: %u", s_prog.idx);
    // The forward pair comes up with the deferred one, not when the first
    // blended effect happens to be drawn: the reference compiles every particle
    // material while the shaders are loaded (CBlender_Compile::Compile,
    // Blender_Particle.cpp:74-175, reached from CRender::load_shaders), so both
    // pixel stages exist before any effect asks for them. It also keeps the
    // builds off the draw path once the frame has begun.
    EnsureForwardPrograms();
    return true;
}

// The forward pair. Built together with the deferred program rather than on the
// first blended draw, because the reference compiles every particle material
// while the shaders load (CBlender_Compile::Compile, Blender_Particle.cpp:74-175,
// reached from CRender::load_shaders) - so both pixel stages exist before any
// effect asks for them, and no .sc compile happens mid-frame.
bool EnsureForwardPrograms()
{
    if (bgfxIsValid(s_progFwd) && bgfxIsValid(s_progFwdAdd))
        return true;
    EnsureLayout();
    EnsureUniforms();
    if (!bgfxIsValid(s_progFwd))
    {
        s_progFwd = BuildProgram("particle_fwd_vs.sc", "particle_fwd_ps.sc");
        if (bgfxIsValid(s_progFwd))
            LogInfo("[BGFX] Particle forward program created: %u", s_progFwd.idx);
    }
    if (!bgfxIsValid(s_progFwdAdd))
    {
        s_progFwdAdd = BuildProgram("particle_fwd_vs.sc", "particle_fwd_add_ps.sc");
        if (bgfxIsValid(s_progFwdAdd))
            LogInfo("[BGFX] Particle forward+ program created: %u", s_progFwdAdd.idx);
    }
    return bgfxIsValid(s_progFwd) && bgfxIsValid(s_progFwdAdd);
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

// cl_fog_plane, Blender_Recorder_StandartBinding.cpp:138-162, verbatim:
// the plane through the far plane of mFullTransform, normalised, then folded
// with fog_near/fog_far into the row calc_fogging (r2/common.h:72) dot()s
// against. A = fog_near, B = 1/(fog_far - fog_near).
void BindFogPlane()
{
    CEnvDescriptorMixer* env = g_pGamePersistent
        ? g_pGamePersistent->Environment().CurrentEnv
        : nullptr;
    if (!env || !bgfxIsValid(s_fogPlane))
        return;

    Fmatrix& M = Device.mFullTransform;
    float plane[4];
    plane[0] = -(M._14 + M._13);
    plane[1] = -(M._24 + M._23);
    plane[2] = -(M._34 + M._33);
    plane[3] = -(M._44 + M._43);
    const float denom = -1.0f / _sqrt(_sqr(plane[0]) + _sqr(plane[1]) + _sqr(plane[2]));
    plane[0] *= denom;
    plane[1] *= denom;
    plane[2] *= denom;
    plane[3] *= denom;

    const float A = env->fog_near;
    const float B = 1.f / (env->fog_far - A);
    const float fogPlane[4] =
    {
        -plane[0] * B,
        -plane[1] * B,
        -plane[2] * B,
        1.f - (plane[3] - A) * B
    };
    bgfx_set_uniform(s_fogPlane, fogPlane, 1);
}

// Uploads the quads in transient chunks. setupView binds the view first, which
// the deferred route needs (it owns kView) and the forward route does not (the
// whole queue shares one view setup done once by RenderForwardPass).
bool EmitQuads(bgfx_program_handle_t prog, bgfx_view_id_t view,
    bgfx_texture_handle_t tex, const Vertex* quads, u32 quadCount,
    u64 state, bool setupView, const Fmatrix* projOverride)
{
    if (!bgfxIsValid(prog) || !bgfxIsValid(tex) || !quads || !quadCount)
        return false;

    if (setupView)
        SetupView(projOverride);

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
        bgfx_submit(view, prog, 0, BGFX_DISCARD_ALL);

        done += batch;
    }
    return true;
}

}

void OnDeviceCreate()
{
    EnsureProgram();
}

void OnDeviceDestroy()
{
    if (bgfxIsValid(s_prog)) { bgfx_destroy_program(s_prog); s_prog = BGFX_INVALID_HANDLE; }
    if (bgfxIsValid(s_progFwd)) { bgfx_destroy_program(s_progFwd); s_progFwd = BGFX_INVALID_HANDLE; }
    if (bgfxIsValid(s_progFwdAdd)) { bgfx_destroy_program(s_progFwdAdd); s_progFwdAdd = BGFX_INVALID_HANDLE; }
    if (bgfxIsValid(s_base)) { bgfx_destroy_uniform(s_base); s_base = BGFX_INVALID_HANDLE; }
    if (bgfxIsValid(s_params)) { bgfx_destroy_uniform(s_params); s_params = BGFX_INVALID_HANDLE; }
    if (bgfxIsValid(s_fogPlane)) { bgfx_destroy_uniform(s_fogPlane); s_fogPlane = BGFX_INVALID_HANDLE; }
    s_forward.clear();
    s_forwardFb = BGFX_INVALID_HANDLE;
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

    // Keyed by the normalized single name, never by the raw CPEDef::m_TextureName:
    // a miss cached under the unparsed list string would survive the parse fix.
    std::string key;
    NormalizeTextureName(name, key);
    if (key.empty())
        return BGFX_INVALID_HANDLE;

    auto it = s_texCache.find(key);
    if (it != s_texCache.end())
        return it->second;

    bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;
    unsigned int w = 0, h = 0;
    if (bgfxLoadWorldTexture(key.c_str(), tex, w, h))
    {
        LogInfo("[BGFX] Particle '%s' %ux%u h=%u", key.c_str(), w, h, tex.idx);
    }
    else
    {
        // A miss is cached under the same key: the reference resolves a
        // particle texture by name once, so a missing file must not be
        // re-read and re-logged on every frame.
        tex = BGFX_INVALID_HANDLE;
        LogError("[BGFX] Particle: failed to load '%s'", key.c_str());
    }
    s_texCache[key] = tex;
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

    float prm[4] = { (float)alphaRef, alphaTest ? 1.f : 0.f, 0.f, 0.f };
    bgfx_set_uniform(s_params, prm, 1);
    return EmitQuads(s_prog, kView, tex, quads, quadCount,
        BlendState(blendMode, writeZ), true, projOverride);
}

ForwardPixel ForwardPixelForShader(LPCSTR shaderName)
{
    // The pixel stage is whatever the material's own shader:begin names, and the
    // shipped particle material set is three files
    // (game_unpacked\shaders\r3\particles_*.s), of which exactly two name
    // particle_add: particles_add.s:2 and particles_xadd.s:2. There is no
    // particles_alpha_add.s, no particles_aadd.s and no particles_set.s in r3, so
    // every other particle material - and every CBlender_Particle pass but SET
    // (Blender_Particle.cpp:127-131) - names "particle".
    //
    // The test is therefore the material name and NOT "does it contain add": a
    // particles\alpha_add effect binds particle.ps in the reference, because
    // oBlend==5 is one of the five passes that use ("particle","particle"), and a
    // substring test on "add" would have handed it particle_add.ps.
    //
    // SET is not decided here: it is the blender's oBlend==0, which arrives as
    // m_BlendMode and is handled by the caller (Blender_Particle.cpp:126).
    if (!shaderName || !shaderName[0])
        return FWD_PIXEL_BLEND;

    std::string s(shaderName);
    for (char& c : s)
    {
        c = (char)std::tolower((unsigned char)c);
        // _lua_Create undecorates the shader name before looking the script up:
        // '\\' becomes '_' (dx10ResourceManager_Scripting.cpp:339-342), so
        // "particles\xadd" and "particles_xadd" are the same material.
        if (c == '\\')
            c = '_';
    }
    if (s == "particles_add" || s == "particles_xadd")
        return FWD_PIXEL_ADD;
    return FWD_PIXEL_BLEND;
}

void SetForwardFrameBuffer(bgfx_frame_buffer_handle_t fb)
{
    s_forwardFb = fb;
}

void RenderForwardPass()
{
    if (s_forward.empty() || !bgfxIsValid(s_forwardFb))
    {
        s_forward.clear();
        return;
    }

    // CRender::render_forward (r4_R_render.cpp:617-640) as bgfxHDR::phase_combine
    // enters it: the pass re-uses the camera view, the colour target phase_combine
    // bound (rt_Generic_0, r4_rendertarget_phase_combine.cpp:378) and the scene
    // depth it came with (HW.pBaseZB in the same u_setrt), culls CCW and leaves
    // the stencil off. bgfx keeps the order by view id, so the id is the HDR
    // module's kForwardView and the frame buffer is the one it hands over.
    bgfx_set_view_frame_buffer(bgfxHDR::kForwardView, s_forwardFb);
    bgfx_set_view_rect(bgfxHDR::kForwardView, 0, 0, (u16)Device.dwWidth, (u16)Device.dwHeight);
    bgfx_set_view_clear(bgfxHDR::kForwardView, BGFX_CLEAR_NONE, 0, 1.0f, 0);
    bgfx_set_view_mode(bgfxHDR::kForwardView, BGFX_VIEW_MODE_SEQUENTIAL);
    bgfx_set_view_transform(bgfxHDR::kForwardView, Device.mView.m, Device.mProject.m);
    bgfx_touch(bgfxHDR::kForwardView);

    BindFogPlane();

    for (const ForwardEffect& fx : s_forward)
    {
        // The forward materials are `: zb (true, false)` (particles_add.s:5) -
        // depth test on, depth write off - the quads must not occlude each other
        // or anything drawn after the pass.
        const u64 state = BlendState(fx.blendMode, false);
        const bgfx_program_handle_t prog = (fx.pixel == FWD_PIXEL_ADD) ? s_progFwdAdd : s_progFwd;
        EmitQuads(prog, bgfxHDR::kForwardView, fx.tex, fx.quads.data(),
            (u32)fx.quads.size() / 4u, state, false, &fx.proj);
    }
    s_forward.clear();
}

bool SubmitPAPI(PAPI::Particle* particles, u32 count, PS::CPEDef* def,
    int blendMode, const Fmatrix* xformOrNull, const Fmatrix* projOverride,
    LPCSTR shaderName)
{
    if (!particles || !count || !def || !def->m_Flags.is(PS::CPEDef::dfSprite))
        return false;

    bgfx_texture_handle_t tex = GetTexture(def->m_TextureName.c_str());
    if (!bgfxIsValid(tex))
        return false;

    // CBlender_Particle::Compile (Blender_Particle.cpp:122-132) hands only
    // oBlend==0 to deffer_particle, i.e. the G-buffer; BLEND, ADD, MUL, MUL_2X
    // and ALPHA-ADD are forward passes that CRender::render_forward draws after
    // the combine. The forward route needs the forward programs and a target to
    // draw into; without either the effect stays deferred, which is the frame
    // this port produced before the split existed.
    const ForwardPixel pixel = (blendMode == BLEND_SET)
        ? FWD_PIXEL_NONE
        : ForwardPixelForShader(shaderName);
    const bool forward = (pixel != FWD_PIXEL_NONE)
        && bgfxIsValid(s_forwardFb)
        && EnsureForwardPrograms();

    bool framed = def->m_Flags.is(PS::CPEDef::dfFramed);
    bool velScale = def->m_Flags.is(PS::CPEDef::dfVelocityScale);
    // The three flags that pick the quad's axes (ParticleEffect.cpp:734-814).
    const bool alignToPath = def->m_Flags.is(PS::CPEDef::dfAlignToPath);
    const bool worldAlign  = def->m_Flags.is(PS::CPEDef::dfWorldAlign);
    const bool faceAlign   = def->m_Flags.is(PS::CPEDef::dfFaceAlign);

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

        // The reference packs the particle colour into the vertex the same way
        // (m.color straight into FVF::LIT::color, ParticleEffect.cpp:747/773/794
        // pass m.color through FillSprite), and the port's PackColor keeps the
        // swizzle that unpack_D3DCOLOR undoes in the vertex stage
        // (r3/particle.vs:33).
        u32 color;
        PackColor(m.color, color);

        Vertex* out = &quads[(size_t)i * 4];

        // The reference picks the quad's axes from three CPEDef flags
        // (CParticleEffect::Render, ParticleEffect.cpp:734-814): a material that
        // is not dfAlignToPath gets the camera billboard, and one that is gets
        // its quad aligned to the path, either from the definition's default
        // rotation (dfWorldAlign) or from the particle's velocity (dfFaceAlign).
        // The port drew the camera billboard for all of them, so an aligned
        // effect came out facing the camera instead of its path.
        Fvector pos = m.pos;
        if (xformOrNull)
            xformOrNull->transform_tiny(pos, m.pos);

        if (alignToPath)
        {
            const float speed = m.vel.magnitude();
            if (worldAlign && (speed < EPS_S))
            {
                // ParticleEffect.cpp:737-753: the definition's own rotation.
                Fmatrix M;
                M.setXYZ(def->m_APDefaultRotation);
                if (xformOrNull)
                    M.mulA_43(*xformOrNull);
                BuildBillboardQuad(out, M.k, M.i, pos, r_x, r_y, sina, cosa, lt, rb, color);
            }
            else if (faceAlign && (speed >= EPS_S))
            {
                // ParticleEffect.cpp:754-779: an orthonormal frame whose k axis is
                // the velocity, with the same 0.99 degeneracy guard.
                Fmatrix M;
                M.identity();
                M.k.div(m.vel, speed);
                M.j.set(0, 1, 0);
                if (_abs(M.j.dotproduct(M.k)) > 0.99f)
                    M.j.set(0, 0, 1);
                M.i.crossproduct(M.j, M.k);
                M.i.normalize();
                M.j.crossproduct(M.k, M.i);
                M.j.normalize();
                if (xformOrNull)
                    M.mulA_43(*xformOrNull);
                BuildBillboardQuad(out, M.j, M.i, pos, r_x, r_y, sina, cosa, lt, rb, color);
            }
            else
            {
                // ParticleEffect.cpp:780-800: a single direction, with the right
                // axis taken as the cross product of that direction and the view
                // direction (FillSprite_fpu, ParticleEffect.cpp:370-418, whose
                // commented-out R.crossproduct(T, vCameraDirection) is the
                // scalar form of the SSE code below it).
                Fvector dir;
                if (speed >= EPS_S)
                    dir.div(m.vel, speed);
                else
                    dir.setHP(-def->m_APDefaultRotation.y, -def->m_APDefaultRotation.x);

                Fvector right;
                right.crossproduct(dir, Device.vCameraDirection);
                if (xformOrNull)
                {
                    // ParticleEffect.cpp:789-795: the parent transform moves the
                    // position and turns the direction, and the right axis is
                    // then taken from the transformed direction.
                    Fvector d;
                    xformOrNull->transform_dir(d, dir);
                    dir = d;
                    d.crossproduct(dir, Device.vCameraDirection);
                    right = d;
                }
                right.normalize_safe();
                BuildBillboardQuad(out, dir, right, pos, r_x, r_y, sina, cosa, lt, rb, color);
            }
        }
        else
        {
            // ParticleEffect.cpp:802-814, the camera billboard.
            BuildCameraBillboard(out, pos, r_x, r_y, sina, cosa, lt, rb, color);
        }
    }

    if (forward)
    {
        // Queued instead of submitted: bgfx draws by view id and this view sits
        // after the combine, so the effect has to be held until the pass runs.
        // The queue keeps the submission order, which is the front-to-back order
        // of the dynamic walk (bgfxModelBridge.cpp) the reference gets from
        // std::sort(lstRenderablesMain, pred_sp_sort) - r4_R_render.cpp:68.
        // A frame that never reaches RenderForwardPass (target not bound yet)
        // drops its own leftovers here instead of bleeding them into the next.
        if (s_forwardFrame != Device.dwFrame)
        {
            s_forward.clear();
            s_forwardFrame = Device.dwFrame;
        }
        ForwardEffect& fx = s_forward.emplace_back();
        fx.quads.swap(quads);
        fx.tex        = tex;
        fx.blendMode  = blendMode;
        fx.pixel      = pixel;
        fx.proj       = projOverride ? *projOverride : Device.mProject;
        return true;
    }

    const bool isSet = (blendMode == BLEND_SET);
    return Submit(tex, quads.data(), count, blendMode,
        isSet, isSet, 200, projOverride);
}

}
