#include "stdafx.h"
#pragma hdrstop

#include "SH_RT.h"
#include "dxRenderDeviceRender.h"

// BGFX port: render targets are stubbed.

CRT::CRT			()
{
	pSurface		= NULL;
	pRT				= NULL;
	dwWidth			= 0;
	dwHeight		= 0;
	fmt				= D3DFMT_UNKNOWN;
	_order			= 0;
}

CRT::~CRT			()
{
	destroy();
}

void CRT::create	(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount )
{
	(void)Name; (void)w; (void)h; (void)f; (void)SampleCount;
	dwWidth			= w;
	dwHeight		= h;
	fmt				= f;
}

void CRT::destroy	()
{
	pTexture._set(NULL);
	_RELEASE(pRT);
	_RELEASE(pSurface);
}

void CRT::reset_begin	()	{ destroy(); }
void CRT::reset_end		()	{}

void resptrcode_crt::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount)
{
	(void)Name; (void)SampleCount;
	CRT* rt = xr_new<CRT>();
	rt->create(Name, w, h, f, 1);
	_set(rt);
}