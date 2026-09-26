#include "stdafx.h"
#include "bgfxEnvironmentRender.h"
#include "bgfx_capi.h"
#include "bgfxShaderCompiler.h"
#include "bgfxUIShader.h"
#include "port/bgfxHDR.h"

// Real particle-systems library (port/PSLibrary.cpp). Declared on the
// interface type only so this non-port TU does not pull the port stdafx.
particles_systems::library_interface const& bgfxPSLibraryInterface();

#include "../../xrcdb/xrXRC.h"
#include "../../xrSound/sound.h"
#include "../../xrEngine/Environment.h"
#include "../../xrEngine/device.h"
#include "../../xrEngine/xr_efflensflare.h"

#include <map>
#include <string>
#include <vector>

extern "C"
{
    bgfx_texture_handle_t bgfx_create_texture_cube(uint16_t _size, bool _hasMips, uint16_t _numLayers, bgfx_texture_format_t _format, uint64_t _flags, const bgfx_memory_t* _mem, uint64_t _external);
    void bgfx_update_texture_cube(bgfx_texture_handle_t _handle, uint16_t _layer, uint8_t _side, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height, const bgfx_memory_t* _mem, uint16_t _pitch);
}

namespace
{
    const bgfx_view_id_t kSkyView = 0;

    Fvector3 hbox_verts[24] =
    {
        {-1.f, -1.f, -1.f}, {-1.f, -1.01f, -1.f},
        { 1.f, -1.f, -1.f}, { 1.f, -1.01f, -1.f},
        {-1.f, -1.f,  1.f}, {-1.f, -1.01f,  1.f},
        { 1.f, -1.f,  1.f}, { 1.f, -1.01f,  1.f},
        {-1.f,  1.f, -1.f}, {-1.f,  1.f, -1.f},
        { 1.f,  1.f, -1.f}, { 1.f,  1.f, -1.f},
        {-1.f,  1.f,  1.f}, {-1.f,  1.f,  1.f},
        { 1.f,  1.f,  1.f}, { 1.f,  1.f,  1.f},
        {-1.f,  0.f, -1.f}, {-1.f, -1.f, -1.f},
        { 1.f,  0.f, -1.f}, { 1.f, -1.f, -1.f},
        { 1.f,  0.f,  1.f}, { 1.f, -1.f,  1.f},
        {-1.f,  0.f,  1.f}, {-1.f, -1.f,  1.f}
    };

    u16 hbox_faces[20 * 3] =
    {
        0,  2,  3,
        3,  1,  0,
        4,  5,  7,
        7,  6,  4,
        0,  1,  9,
        9,  8,  0,
        8,  9,  5,
        5,  4,  8,
        1,  3, 10,
        10,  9,  1,
        9, 10,  7,
        7,  5,  9,
        3,  2, 11,
        11, 10,  3,
        10, 11,  6,
        6,  7, 10,
        2,  0,  8,
        8, 11,  2,
        11,  8,  4,
        4,  6, 11
    };

    struct SkyVertex
    {
        Fvector pos;
        u32 color;
        Fvector dir;
    };

    struct CloudsVertex
    {
        Fvector pos;
        u32 color0;
        u32 color1;
        Fvector2 uv0;
        Fvector2 uv1;
    };

    void PackColor(u32 C, u32& out)
    {
        out = (C & 0xFF00FF00u) | ((C >> 16) & 0x000000FFu) | ((C << 16) & 0x00FF0000u);
    }

    static const float s_identityXform[16] =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    bgfx_program_handle_t s_skyProg = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sky0 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_sky1 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_skyTonemap = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_skyFogColor = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_skyLayout = {};
    bool s_skyLayoutReady = false;

    bgfx_program_handle_t s_cloudsProg = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_clouds0 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_clouds1 = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_cloudsTonemap = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_cloudsFogColor = BGFX_INVALID_HANDLE;
    bgfx_uniform_handle_t s_cloudsTime = BGFX_INVALID_HANDLE;
    bgfx_vertex_layout_t s_cloudsLayout = {};
    bool s_cloudsLayoutReady = false;

    bool s_envReady = false;

    std::map<std::string, bgfx_texture_handle_t> s_cubeCache;

    bgfx_program_handle_t BuildEnvProgram(const char* vsFile, const char* psFile)
    {
        std::vector<uint8_t> vsBlob;
        std::vector<uint8_t> psBlob;
        if (!bgfxShaderCompileFile(vsFile, 'v', vsBlob) || !bgfxShaderCompileFile(psFile, 'f', psBlob))
            return BGFX_INVALID_HANDLE;
        if (vsBlob.empty() || psBlob.empty())
            return BGFX_INVALID_HANDLE;
        bgfx_shader_handle_t vsh = bgfx_create_shader(bgfx_copy(vsBlob.data(), (u32)vsBlob.size()));
        bgfx_shader_handle_t fsh = bgfx_create_shader(bgfx_copy(psBlob.data(), (u32)psBlob.size()));
        if (!bgfxIsValid(vsh) || !bgfxIsValid(fsh))
            return BGFX_INVALID_HANDLE;
        return bgfx_create_program(vsh, fsh, true);
    }

    void EnsureEnvResources()
    {
        if (s_envReady && bgfxIsValid(s_skyProg) && bgfxIsValid(s_cloudsProg))
            return;

        if (!s_skyLayoutReady)
        {
            bgfx_vertex_layout_begin(&s_skyLayout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_skyLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_skyLayout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
            bgfx_vertex_layout_add(&s_skyLayout, BGFX_ATTRIB_TEXCOORD2, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_skyLayout);
            s_skyLayoutReady = true;
        }

        if (!s_cloudsLayoutReady)
        {
            bgfx_vertex_layout_begin(&s_cloudsLayout, bgfx_get_renderer_type());
            bgfx_vertex_layout_add(&s_cloudsLayout, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_cloudsLayout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
            bgfx_vertex_layout_add(&s_cloudsLayout, BGFX_ATTRIB_COLOR1, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
            bgfx_vertex_layout_add(&s_cloudsLayout, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_add(&s_cloudsLayout, BGFX_ATTRIB_TEXCOORD1, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
            bgfx_vertex_layout_end(&s_cloudsLayout);
            s_cloudsLayoutReady = true;
        }

        if (!bgfxIsValid(s_skyProg))
        {
            s_skyProg = BuildEnvProgram("skybox_2t_vs.sc", "skybox_2t_ps.sc");
            if (bgfxIsValid(s_skyProg))
            {
                s_sky0 = bgfx_create_uniform("s_sky0", BGFX_UNIFORM_TYPE_SAMPLER, 1);
                s_sky1 = bgfx_create_uniform("s_sky1", BGFX_UNIFORM_TYPE_SAMPLER, 1);
                s_skyTonemap = bgfx_create_uniform("s_tonemap", BGFX_UNIFORM_TYPE_SAMPLER, 1);
                s_skyFogColor = bgfx_create_uniform("u_fogColor", BGFX_UNIFORM_TYPE_VEC4, 1);
                LogInfo("[BGFX] Sky program created: %u", s_skyProg.idx);
            }
            else
                LogError("[BGFX] Sky program build failed");
        }

        if (!bgfxIsValid(s_cloudsProg))
        {
            s_cloudsProg = BuildEnvProgram("clouds_vs.sc", "clouds_ps.sc");
            if (bgfxIsValid(s_cloudsProg))
            {
                s_clouds0 = bgfx_create_uniform("s_clouds0", BGFX_UNIFORM_TYPE_SAMPLER, 1);
                s_clouds1 = bgfx_create_uniform("s_clouds1", BGFX_UNIFORM_TYPE_SAMPLER, 1);
                s_cloudsTonemap = bgfx_create_uniform("s_tonemap", BGFX_UNIFORM_TYPE_SAMPLER, 1);
                s_cloudsFogColor = bgfx_create_uniform("u_fogColor", BGFX_UNIFORM_TYPE_VEC4, 1);
                s_cloudsTime = bgfx_create_uniform("u_cloudsTime", BGFX_UNIFORM_TYPE_VEC4, 1);
                LogInfo("[BGFX] Clouds program created: %u", s_cloudsProg.idx);
            }
            else
                LogError("[BGFX] Clouds program build failed");
        }

        s_envReady = bgfxIsValid(s_skyProg) && bgfxIsValid(s_cloudsProg);
    }

    void ReleaseEnvResources()
    {
        if (bgfxIsValid(s_skyProg)) { bgfx_destroy_program(s_skyProg); s_skyProg = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_sky0)) { bgfx_destroy_uniform(s_sky0); s_sky0 = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_sky1)) { bgfx_destroy_uniform(s_sky1); s_sky1 = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_skyTonemap)) { bgfx_destroy_uniform(s_skyTonemap); s_skyTonemap = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_skyFogColor)) { bgfx_destroy_uniform(s_skyFogColor); s_skyFogColor = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_cloudsProg)) { bgfx_destroy_program(s_cloudsProg); s_cloudsProg = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_clouds0)) { bgfx_destroy_uniform(s_clouds0); s_clouds0 = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_clouds1)) { bgfx_destroy_uniform(s_clouds1); s_clouds1 = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_cloudsTonemap)) { bgfx_destroy_uniform(s_cloudsTonemap); s_cloudsTonemap = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_cloudsFogColor)) { bgfx_destroy_uniform(s_cloudsFogColor); s_cloudsFogColor = BGFX_INVALID_HANDLE; }
        if (bgfxIsValid(s_cloudsTime)) { bgfx_destroy_uniform(s_cloudsTime); s_cloudsTime = BGFX_INVALID_HANDLE; }
        for (auto& e : s_cubeCache)
            if (bgfxIsValid(e.second))
                bgfx_destroy_texture(e.second);
        s_cubeCache.clear();
        s_skyLayoutReady = false;
        s_cloudsLayoutReady = false;
        s_envReady = false;
    }

    // Horizon fog color from weather (same fog_color as world fog).
    void SetEnvFogColor(bgfx_uniform_handle_t uniform, CEnvDescriptorMixer* E)
    {
        float fogColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (E)
        {
            fogColor[0] = E->fog_color.x;
            fogColor[1] = E->fog_color.y;
            fogColor[2] = E->fog_color.z;
            fogColor[3] = E->fog_density;
        }
        if (bgfxIsValid(uniform))
            bgfx_set_uniform(uniform, fogColor, 1);
    }

    bool SkyTexNameEmpty(const shared_str& n)
    {
        LPCSTR s = n.c_str();
        if (!s || !s[0])
            return true;
        if (s[0] == '$')
            return true;
        return 0 == _strnicmp(s, "null", 4);
    }

#pragma pack(push, 1)
    struct SkyDdsPixelFormat
    {
        u32 size;
        u32 flags;
        u32 fourcc;
        u32 rgbBitCount;
        u32 rMask;
        u32 gMask;
        u32 bMask;
        u32 aMask;
    };

    struct SkyDdsHeader
    {
        u32 size;
        u32 flags;
        u32 height;
        u32 width;
        u32 pitchOrLinearSize;
        u32 depth;
        u32 mipMapCount;
        u32 reserved[11];
        SkyDdsPixelFormat ddspf;
        u32 caps;
        u32 caps2;
        u32 caps3;
        u32 caps4;
        u32 reserved2;
    };
#pragma pack(pop)

#define SKY_FOURCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

    bgfx_texture_handle_t LoadSkyCube(const shared_str& name)
    {
        if (SkyTexNameEmpty(name))
            return BGFX_INVALID_HANDLE;

        std::string key(name.c_str());
        auto it = s_cubeCache.find(key);
        if (it != s_cubeCache.end())
            return it->second;

        bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;

        char path[260];
        strconcat(sizeof(path), path, name.c_str(), ".dds");
        IReader* file = FS.r_open("$game_textures$", path);
        if (!file)
        {
            LogError("[BGFX] Sky: cube file not found: '%s'", name.c_str());
            return tex;
        }

        do
        {
            u32 size = file->elapsed();
            if (size < 4 + sizeof(SkyDdsHeader))
                break;
            u8* data = (u8*)xr_malloc(size);
            file->r(data, (int)size);

            bool ok = false;
            if (data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
            {
                SkyDdsHeader hdr;
                memcpy(&hdr, data + 4, sizeof(hdr));
                u32 w = hdr.width;
                u32 h = hdr.height;
                bool isCube = (hdr.caps2 & 0x0000FE00u) == 0x0000FE00u;
                bool singleLevel = hdr.mipMapCount <= 1;
                if (w == h && w >= 4 && isCube && singleLevel)
                {
                    bgfx_texture_format_t fmt = BGFX_TEXTURE_FORMAT_COUNT;
                    u32 faceBytes = 0;
                    u16 pitch = 0;
                    u32 fourcc = hdr.ddspf.fourcc;
                    bool isDxt = (hdr.ddspf.flags & 0x00000004u) != 0;
                    bool isRgb = (hdr.ddspf.flags & 0x00000040u) != 0;
                    if (isDxt && fourcc == SKY_FOURCC('D', 'X', 'T', '1'))
                    {
                        fmt = BGFX_TEXTURE_FORMAT_BC1;
                        faceBytes = ((w + 3) / 4) * ((h + 3) / 4) * 8;
                        pitch = (u16)(((w + 3) / 4) * 8);
                    }
                    else if (isDxt && (fourcc == SKY_FOURCC('D', 'X', 'T', '3') || fourcc == SKY_FOURCC('D', 'X', 'T', '2')))
                    {
                        fmt = BGFX_TEXTURE_FORMAT_BC2;
                        faceBytes = ((w + 3) / 4) * ((h + 3) / 4) * 16;
                        pitch = (u16)(((w + 3) / 4) * 16);
                    }
                    else if (isDxt && (fourcc == SKY_FOURCC('D', 'X', 'T', '5') || fourcc == SKY_FOURCC('D', 'X', 'T', '4')))
                    {
                        fmt = BGFX_TEXTURE_FORMAT_BC3;
                        faceBytes = ((w + 3) / 4) * ((h + 3) / 4) * 16;
                        pitch = (u16)(((w + 3) / 4) * 16);
                    }
                    else if (!isDxt && isRgb && hdr.ddspf.rgbBitCount == 32)
                    {
                        fmt = BGFX_TEXTURE_FORMAT_BGRA8;
                        faceBytes = w * h * 4;
                        pitch = (u16)(w * 4);
                    }
                    if (fmt != BGFX_TEXTURE_FORMAT_COUNT && 4 + sizeof(SkyDdsHeader) + 6 * faceBytes <= size)
                    {
                        tex = bgfx_create_texture_cube((u16)w, false, 1, fmt,
                            BGFX_TEXTURE_U_CLAMP | BGFX_TEXTURE_V_CLAMP, nullptr, 0);
                        if (bgfxIsValid(tex))
                        {
                            ok = true;
                            for (u8 side = 0; side < 6; side++)
                            {
                                const u8* face = data + 4 + sizeof(SkyDdsHeader) + (u32)side * faceBytes;
                                bgfx_update_texture_cube(tex, 0, side, 0, 0, 0, (u16)w, (u16)h,
                                    bgfx_copy(face, faceBytes), pitch);
                            }
                        }
                    }
                }
            }
            xr_free(data);

            if (ok)
                LogInfo("[BGFX] Sky cube '%s' h=%u", name.c_str(), tex.idx);
            else
                LogError("[BGFX] Sky: failed to load cube '%s'", name.c_str());
        } while (false);

        FS.r_close(file);
        if (bgfxIsValid(tex))
            s_cubeCache[key] = tex;
        return tex;
    }

    bgfx_texture_handle_t LoadCloudsMap(const shared_str& name)
    {
        if (SkyTexNameEmpty(name))
            return BGFX_INVALID_HANDLE;
        bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;
        unsigned int w = 0, h = 0;
        if (bgfxLoadWorldTexture(name.c_str(), tex, w, h))
            LogInfo("[BGFX] Clouds '%s' %ux%u h=%u", name.c_str(), w, h, tex.idx);
        else
            LogError("[BGFX] Clouds: failed to load '%s'", name.c_str());
        return tex;
    }

}

void bgfxEnvDescriptorRender::Copy(IEnvDescriptorRender &_in)
{
    *this = *(bgfxEnvDescriptorRender*)&_in;
}

void bgfxEnvDescriptorRender::OnDeviceCreate(CEnvDescriptor &owner)
{
    owner;
}

void bgfxEnvDescriptorRender::OnDeviceDestroy()
{
    sky_texture = BGFX_INVALID_HANDLE;
    sky_texture_env = BGFX_INVALID_HANDLE;
    clouds_texture = BGFX_INVALID_HANDLE;
    b_textures_loaded = false;
}

void bgfxEnvDescriptorRender::OnPrepare(CEnvDescriptor& owner)
{
    if (b_textures_loaded)
        return;
    sky_texture = LoadSkyCube(owner.sky_texture_name);
    sky_texture_env = LoadSkyCube(owner.sky_texture_env_name);
    clouds_texture = LoadCloudsMap(owner.clouds_texture_name);
    b_textures_loaded = true;
}

void bgfxEnvDescriptorRender::OnUnload(CEnvDescriptor& owner)
{
    owner;
    if (!b_textures_loaded)
        return;
    sky_texture = BGFX_INVALID_HANDLE;
    sky_texture_env = BGFX_INVALID_HANDLE;
    clouds_texture = BGFX_INVALID_HANDLE;
    b_textures_loaded = false;
}

void bgfxEnvDescriptorMixerRender::Copy(IEnvDescriptorMixerRender &_in)
{
    *this = *(bgfxEnvDescriptorMixerRender*)&_in;
}

void bgfxEnvDescriptorMixerRender::Destroy()
{
    sky_a = BGFX_INVALID_HANDLE;
    sky_b = BGFX_INVALID_HANDLE;
    sky_env_a = BGFX_INVALID_HANDLE;
    sky_env_b = BGFX_INVALID_HANDLE;
    clouds_a = BGFX_INVALID_HANDLE;
    clouds_b = BGFX_INVALID_HANDLE;
}

void bgfxEnvDescriptorMixerRender::Clear()
{
    Destroy();
}

void bgfxEnvDescriptorMixerRender::lerp(IEnvDescriptorRender *inA, IEnvDescriptorRender *inB)
{
    bgfxEnvDescriptorRender *pA = (bgfxEnvDescriptorRender *)inA;
    bgfxEnvDescriptorRender *pB = (bgfxEnvDescriptorRender *)inB;
    sky_a = pA->sky_texture;
    sky_b = pB->sky_texture;
    sky_env_a = pA->sky_texture_env;
    sky_env_b = pB->sky_texture_env;
    clouds_a = pA->clouds_texture;
    clouds_b = pB->clouds_texture;
}

void bgfxEnvironmentRender::Copy(IEnvironmentRender &_in)
{
    _in;
}

void bgfxEnvironmentRender::OnFrame(CEnvironment &env)
{
    env;
}

void bgfxEnvironmentRender::OnLoad()
{
}

void bgfxEnvironmentRender::OnUnload()
{
}

void bgfxEnvironmentRender::RenderSky(CEnvironment &env)
{
    CEnvDescriptorMixer* E = env.CurrentEnv;
    if (!E)
        return;
    bgfxEnvDescriptorMixerRender* mix = (bgfxEnvDescriptorMixerRender*)&*E->m_pDescriptorMixer;
    if (!mix)
        return;

    EnsureEnvResources();

    bgfx_texture_handle_t tonemapTex = bgfxHDR::GetTonemapTexture();
    if (bgfxIsValid(s_skyProg) && bgfxIsValid(tonemapTex) && bgfxIsValid(mix->sky_a) && bgfxIsValid(mix->sky_b))
    {
        Fmatrix mR;
        mR.identity();
        mR.rotateY(E->sky_rotation);

        u32 C;
        PackColor(color_rgba(iFloor(E->sky_color.x*255.f), iFloor(E->sky_color.y*255.f), iFloor(E->sky_color.z*255.f), iFloor(E->weight*255.f)), C);

        bgfx_transient_vertex_buffer_t tvb;
        bgfx_transient_index_buffer_t tib;
        if (bgfx_alloc_transient_buffers(&tvb, &s_skyLayout, 12, &tib, 60, false))
        {
            SkyVertex* pv = (SkyVertex*)tvb.data;
            for (u32 v = 0; v < 12; v++)
            {
                Fvector src;
                src.set(hbox_verts[v * 2].x, hbox_verts[v * 2].y, hbox_verts[v * 2].z);
                mR.transform_tiny(pv[v].pos, src);
                pv[v].color = C;
                pv[v].dir.set(hbox_verts[v * 2 + 1].x, hbox_verts[v * 2 + 1].y, hbox_verts[v * 2 + 1].z);
            }
            memcpy(tib.data, hbox_faces, sizeof(hbox_faces));

            Fmatrix mT;
            mT.identity();
            mT.translate_over(Device.vCameraPosition);

            bgfx_set_transform(mT.m, 1);
            bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_CULL_CCW, 0);
            bgfx_set_transient_vertex_buffer(0, &tvb, 0, 12);
            bgfx_set_transient_index_buffer(&tib, 0, 60);
            bgfx_set_texture(0, s_sky0, mix->sky_a, 0);
            bgfx_set_texture(1, s_sky1, mix->sky_b, 0);
            bgfx_set_texture(2, s_skyTonemap, tonemapTex, 0);
            SetEnvFogColor(s_skyFogColor, E);
            bgfx_submit(kSkyView, s_skyProg, 0, BGFX_DISCARD_ALL);
            bgfx_set_transform(s_identityXform, 1);
        }
    }

    if (env.eff_LensFlare)
        env.eff_LensFlare->Render(TRUE, FALSE, FALSE);
}

void bgfxEnvironmentRender::RenderClouds(CEnvironment &env)
{
    CEnvDescriptorMixer* E = env.CurrentEnv;
    if (!E)
        return;
    bgfxEnvDescriptorMixerRender* mix = (bgfxEnvDescriptorMixerRender*)&*E->m_pDescriptorMixer;
    if (!mix)
        return;

    EnsureEnvResources();

    bgfx_texture_handle_t tonemapTex = bgfxHDR::GetTonemapTexture();
    if (!bgfxIsValid(s_cloudsProg) || !bgfxIsValid(tonemapTex) || !bgfxIsValid(mix->clouds_a) || !bgfxIsValid(mix->clouds_b))
        return;

    u32 nV = (u32)env.CloudsVerts.size();
    u32 nI = (u32)env.CloudsIndices.size();
    if (!nV || !nI)
        return;

    Fmatrix mXFORM, mScale;
    mScale.scale(10.f, 0.4f, 10.f);
    mXFORM.rotateY(E->sky_rotation);
    mXFORM.mulB_43(mScale);
    mXFORM.translate_over(Device.vCameraPosition);

    Fvector wd0, wd1;
    Fvector4 wind_dir;
    wd0.setHP(PI_DIV_4, 0);
    wd1.setHP(PI_DIV_4 + PI_DIV_8, 0);
    wind_dir.set(wd0.x, wd0.z, wd1.x, wd1.z).mul(0.5f).add(0.5f).mul(255.f);
    u32 C0, C1rgb;
    PackColor(color_rgba(iFloor(wind_dir.x), iFloor(wind_dir.y), iFloor(wind_dir.w), iFloor(wind_dir.z)), C0);
    PackColor(color_rgba(iFloor(E->clouds_color.x*255.f), iFloor(E->clouds_color.y*255.f), iFloor(E->clouds_color.z*255.f), 0), C1rgb);

    bgfx_transient_vertex_buffer_t tvb;
    bgfx_transient_index_buffer_t tib;
    if (!bgfx_alloc_transient_buffers(&tvb, &s_cloudsLayout, nV, &tib, nI, false))
        return;

    CloudsVertex* pv = (CloudsVertex*)tvb.data;
    float t = Device.fTimeGlobal*0.1f;
    Fvector2 d0, d1;
    d0.set(wind_dir.x*(2.f/255.f)-1.f, wind_dir.y*(2.f/255.f)-1.f);
    d1.set(wind_dir.w*(2.f/255.f)-1.f, wind_dir.z*(2.f/255.f)-1.f);
    for (u32 i = 0; i < nV; i++)
    {
        mXFORM.transform_tiny(pv[i].pos, env.CloudsVerts[i]);
        pv[i].color0 = C0;
        float fade = powf(_max(env.CloudsVerts[i].y, 0.f), 25.f);
        u32 aa = (u32)clampr(iFloor(E->clouds_color.w*fade*255.f), 0, 255);
        pv[i].color1 = C1rgb | (aa << 24);
        pv[i].uv0.set(env.CloudsVerts[i].x*0.7f + d0.x*t*0.1f, env.CloudsVerts[i].z*0.7f + d0.y*t*0.1f);
        pv[i].uv1.set(env.CloudsVerts[i].x*2.8f + d1.x*t*0.05f, env.CloudsVerts[i].z*2.8f + d1.y*t*0.05f);
    }
    memcpy(tib.data, &env.CloudsIndices[0], (size_t)nI * sizeof(u16));

    float ct[4] = { Device.fTimeGlobal, 0.f, 0.f, 0.f };
    bgfx_set_uniform(s_cloudsTime, ct, 1);

    bgfx_set_transform(s_identityXform, 1);
    bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_CULL_CCW, 0);
    bgfx_set_transient_vertex_buffer(0, &tvb, 0, nV);
    bgfx_set_transient_index_buffer(&tib, 0, nI);
    bgfx_set_texture(0, s_clouds0, mix->clouds_a, 0);
    bgfx_set_texture(1, s_clouds1, mix->clouds_b, 0);
    bgfx_set_texture(2, s_cloudsTonemap, tonemapTex, 0);
    SetEnvFogColor(s_cloudsFogColor, E);
    bgfx_submit(kSkyView, s_cloudsProg, 0, BGFX_DISCARD_ALL);
}

void bgfxEnvironmentRender::OnDeviceCreate()
{
    EnsureEnvResources();
}

void bgfxEnvironmentRender::OnDeviceDestroy()
{
    ReleaseEnvResources();
}

particles_systems::library_interface const& bgfxEnvironmentRender::particles_systems_library()
{
    return bgfxPSLibraryInterface();
}
