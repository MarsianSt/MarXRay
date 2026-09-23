#pragma once
#include <stdint.h>

// View id used by the 2D UI submits (bgfxUIShader / bgfxFontRender).
//
// Default is 4: the game UI (HUD().RenderUI() in CLevel::OnRender) is submitted
// after Render->Render() and must stay on top of the HUD view (3, actor hands
// and weapon).
//
// While a video frame is being submitted (bgfxUISequenceVideoItem, drawn into
// view 1 with the explicit intent of covering the fullscreen UI backdrop) the
// UI falls back to the world view (0) so the video keeps drawing on top of it.
inline uint16_t& bgfxUISubmitView()
{
	static uint16_t s_view = 4;
	return s_view;
}
