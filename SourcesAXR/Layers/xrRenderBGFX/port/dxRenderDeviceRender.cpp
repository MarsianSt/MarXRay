#include "stdafx.h"
#pragma hdrstop

#include "dxRenderDeviceRender.h"
#include "bgfxResourceManager.h"

dxRenderDeviceRender& dxRenderDeviceRender::Instance()
{
	static dxRenderDeviceRender s_inst;
	return s_inst;
}

dxRenderDeviceRender::dxRenderDeviceRender()
{
	Resources = bgfxResourceManager();
}

void	dxRenderDeviceRender::Copy		(IRenderDeviceRender& _in)						{}
void	dxRenderDeviceRender::setGamma		(float fGamma)								{}
void	dxRenderDeviceRender::setBrightness(float fGamma)								{}
void	dxRenderDeviceRender::setContrast	(float fGamma)								{}
void	dxRenderDeviceRender::updateGamma	()											{}
void	dxRenderDeviceRender::OnDeviceDestroy(BOOL bKeepTextures)						{}
void	dxRenderDeviceRender::ValidateHW	()											{}
void	dxRenderDeviceRender::DestroyHW		()											{}
void	dxRenderDeviceRender::Reset		(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2)	{}
void	dxRenderDeviceRender::SetupStates	()											{}
void	dxRenderDeviceRender::OnDeviceCreate(LPCSTR shName)								{}
void	dxRenderDeviceRender::Create		(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2, bool)	{}
void	dxRenderDeviceRender::SetupGPU		(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF)	{}
void	dxRenderDeviceRender::overdrawBegin	()											{}
void	dxRenderDeviceRender::overdrawEnd	()											{}
void	dxRenderDeviceRender::DeferredLoad	(BOOL E)									{}
void	dxRenderDeviceRender::ResourcesDeferredUpload()								{}
void	dxRenderDeviceRender::ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps)	{}
void	dxRenderDeviceRender::ResourcesDestroyNecessaryTextures()						{}
void	dxRenderDeviceRender::ResourcesStoreNecessaryTextures()						{}
void	dxRenderDeviceRender::ResourcesDumpMemoryUsage()								{}
void	dxRenderDeviceRender::RenderPrefetchUITextures()								{}
bool	dxRenderDeviceRender::HWSupportsShaderYUV2RGB()								{ return false; }
IRenderDeviceRender::DeviceState dxRenderDeviceRender::GetDeviceState()				{ return dsOK; }
BOOL	dxRenderDeviceRender::GetForceGPU_REF()										{ return FALSE; }
u32		dxRenderDeviceRender::GetCacheStatPolys()									{ return 0; }
void	dxRenderDeviceRender::Begin		()												{}
void	dxRenderDeviceRender::Clear		()												{}
void	dxRenderDeviceRender::End			()												{}
void	dxRenderDeviceRender::ClearTarget	()												{}
void	dxRenderDeviceRender::SetCacheXform(Fmatrix& mView, Fmatrix& mProject)				{}
void	dxRenderDeviceRender::OnAssetsChanged()											{}
IResourceManager* dxRenderDeviceRender::GetResourceManager() const						{ return nullptr; }