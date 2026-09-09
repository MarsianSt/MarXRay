#include "stdafx.h"
#include "bgfxLensFlareRender.h"

// bgfxFlareRender
void bgfxFlareRender::Copy(IFlareRender &_in) {}
void bgfxFlareRender::CreateShader(LPCSTR sh_name, LPCSTR tex_name) {}
void bgfxFlareRender::DestroyShader() {}

// bgfxLensFlareRender
void bgfxLensFlareRender::Copy(ILensFlareRender &_in) {}
void bgfxLensFlareRender::Render(CLensFlare &owner, BOOL bSun, BOOL bFlares, BOOL bGradient) {}
void bgfxLensFlareRender::OnDeviceCreate() {}
void bgfxLensFlareRender::OnDeviceDestroy() {}
