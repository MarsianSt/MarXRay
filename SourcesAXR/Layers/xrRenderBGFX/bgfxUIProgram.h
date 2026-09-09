#pragma once
#include "bgfx_capi.h"

// Shared UI vertex layout (defined in xrRenderBGFX.cpp)
extern bgfx_vertex_layout_t g_uiVertexLayout;

// Lazily compiles and creates the UI program (solid color quads for now).
bgfx_program_handle_t bgfxUIProgramGet();
bool bgfxUIProgramValid(bgfx_program_handle_t _h);

// Textured UI program (for video playback).
bgfx_program_handle_t bgfxUITexturedProgramGet();

// Font program (hud_font.ps semantics).
bgfx_program_handle_t bgfxFontProgramGet();

// Texture sampler uniform for the textured program.
bgfx_uniform_handle_t bgfxUITextureSamplerGet();

// 1x1 white texture (fallback when a UI item has no texture).
bgfx_texture_handle_t bgfxUIWhiteTextureGet();
