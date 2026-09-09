#include "stdafx.h"
#pragma hdrstop

#include "SH_Texture.h"
#include "dxRenderDeviceRender.h"

// BGFX port: texture pipeline is stubbed. Textures are never really created,
// loaded or bound - all methods are inert so the class links cleanly.

void resptrcode_texture::create(LPCSTR _name)
{
	(void)_name;
	_set(NULL);
}

CTexture::CTexture()
{
	pSurface			= NULL;
	pAVI				= NULL;
	pTheora				= NULL;
	desc_cache			= 0;
	seqMSPF				= 0;
	flags.MemoryUsage	= 0;
	flags.bLoaded		= false;
	flags.bUser			= false;
	flags.seqCycles		= FALSE;
	m_material			= 1.0f;
}

CTexture::~CTexture()
{
	Unload();
}

void		CTexture::surface_set	(ID3DBaseTexture* surf)	{ if (surf) surf->AddRef(); _RELEASE(pSurface); pSurface = surf; }
ID3DBaseTexture* CTexture::surface_get()						{ if (pSurface) pSurface->AddRef(); return pSurface; }

void		CTexture::apply_load	(u32 stage)		{}
void		CTexture::apply_theora	(u32 stage)		{}
void		CTexture::apply_avi		(u32 stage)		{}
void		CTexture::apply_seq		(u32 stage)		{}
void		CTexture::apply_normal	(u32 stage)		{}
void		CTexture::PostLoad		()				{}
void		CTexture::Preload		()				{}
void		CTexture::Preload		(const char* Name)	{ (void)Name; }
void		CTexture::Load			()				{}
void		CTexture::Load			(const char* Name)	{ (void)Name; flags.bLoaded = true; }
void		CTexture::Unload		()				{ _RELEASE(pSurface); seqDATA.clear(); pSurface = 0; desc_cache = 0; flags.bLoaded = false; }
void		CTexture::desc_update	()				{ }
void		CTexture::video_Play	(BOOL looped, u32 _time)	{}
void		CTexture::video_Pause	(BOOL state)	{}
void		CTexture::video_Stop	()				{}
BOOL		CTexture::video_IsPlaying()				{ return FALSE; }