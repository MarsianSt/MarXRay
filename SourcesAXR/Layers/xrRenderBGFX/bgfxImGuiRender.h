#pragma once
// bgfx output for ImGui (C API). mirror of bgfx-master's ocornut_imgui example:
// draw data -> per-draw-list transient VB/IB -> view 2 on top of world/UI.

// Creates the imgui program (precompiled vs/fs_ocornut_imgui bins), the font
// atlas texture and the s_tex sampler. Requires bgfx initialized, ImGui
// context created and fonts ready. Safe to call again after failure.
bool bgfxImguiInit();

// Submits the current ImGui draw data into bgfx view 2. Call once per frame
// after the world/UI draws, before bgfx_frame().
void bgfxImguiRenderFrame();