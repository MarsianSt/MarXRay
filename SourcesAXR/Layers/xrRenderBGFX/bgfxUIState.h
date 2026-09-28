#pragma once
#include <stdint.h>

#include "port\bgfxHDR.h"

// View id used by the 2D UI submits (bgfxUIShader / bgfxFontRender).
//
// Default is bgfxHDR::kUiView: the game UI (HUD().RenderUI() in CLevel::OnRender)
// is submitted after Render->Render() and must stay on top of the HUD view
// (bgfxHDR::kHudView, actor hands and weapon).
//
// While a video frame is being submitted (bgfxUISequenceVideoItem, drawn into
// bgfxHDR::kIntroView with the explicit intent of covering the fullscreen UI
// backdrop) the UI falls back to the world view (bgfxHDR::kSceneView) so the
// video keeps drawing on top of it.
inline uint16_t& bgfxUISubmitView()
{
	static uint16_t s_view = bgfxHDR::kUiView;
	return s_view;
}
