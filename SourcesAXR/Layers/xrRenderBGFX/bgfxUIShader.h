#pragma once
#include "..\..\Include\xrRender\UIShader.h"
#include "bgfx_capi.h"

#define BGFX_UI_TEX_MIN_ANISOTROPIC UINT64_C(0x00000080)
#define BGFX_UI_TEX_MAG_ANISOTROPIC UINT64_C(0x00000200)

class BgfxVideoDecoder;

class bgfxUIShader : public IUIShader
{
public:
    bgfxUIShader() : m_bInited(false), m_bMovie(false), m_width(0), m_height(0)
    {
        m_texture = BGFX_INVALID_HANDLE;
        m_texName[0] = 0;
        m_seqMSPF = 0;
        m_seqCycles = false;
        m_movieDecoder = nullptr;
        m_movieStartTick = 0;
    }

    virtual void Copy(IUIShader &_in) override;
    virtual void create(LPCSTR sh, LPCSTR tex = 0, bool no_cache = false) override;
    virtual bool inited() override;
    virtual LPCSTR GetTexName() override { return m_texName; }

    // For .seq animations returns the current frame texture; for movie
    // statics (.ogm backgrounds) advances the decoder and returns its
    // current frame.
    bgfx_texture_handle_t GetTexture() const;
    unsigned int GetWidth() const { return m_width; }
    unsigned int GetHeight() const { return m_height; }
    // True when the shader references a sequence-driven video (intro) —
    // its picture is drawn by the video item itself, the UI must skip it.
    bool IsMovie() const { return m_bMovie; }

private:
    bool m_bInited;
    bool m_bMovie;
    bgfx_texture_handle_t m_texture;
    unsigned int m_width;
    unsigned int m_height;
    char m_texName[260];

    // .seq animated textures: list of per-frame textures, like CTexture::apply_seq.
    xr_vector<bgfx_texture_handle_t> m_seqFrames;
    u32 m_seqMSPF;
    bool m_seqCycles;

    // .ogm animated UI statics (menu background videos): decoder plays
    // the video continuously, like CTexture::apply_theora.
    BgfxVideoDecoder* m_movieDecoder;
    mutable u32 m_movieStartTick;
};

// Loads a game texture (DDS/TGA) through the engine VFS. Cached by name.
bool bgfxLoadUITexture(LPCSTR texName, bgfx_texture_handle_t& outTex, unsigned int& outW, unsigned int& outH);
// Loads world/static-geometry textures with wrap sampling (terrain/tiling).
bool bgfxLoadWorldTexture(LPCSTR texName, bgfx_texture_handle_t& outTex, unsigned int& outW, unsigned int& outH);

// Applies the current UI scissor to the next submit (shared with font renderer).
void bgfxUIScissorApply();
