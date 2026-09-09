#ifndef dxRenderDeviceRender_included
#define dxRenderDeviceRender_included
#pragma once

// BGFX port: compatible stand-in for xrRender/dxRenderDeviceRender.h
// The ported visual classes call into dxRenderDeviceRender::Instance() and
// use the DEV macro for resource management. BGFX keeps its own light-weight
// resource layer (bgfxResourceManagerLite), so this class only preserves the
// original interface shape.

#ifndef _EDITOR
	#define DEV bgfxResourceManager()
#else
	#define DEV EDevice.Resources
#endif

#include "..\..\Include\xrRender\RenderDeviceRender.h"
#include "bgfxResourceManager.h"
#include "Shader.h"

class dxRenderDeviceRender : public IRenderDeviceRender
{
public:
	static dxRenderDeviceRender&	Instance();

	dxRenderDeviceRender();

	// IRenderDeviceRender (mostly stubs)
	void			Copy		(IRenderDeviceRender& _in) override;
	void			setGamma	(float fGamma) override;
	void			setBrightness(float fGamma) override;
	void			setContrast	(float fGamma) override;
	void			updateGamma	() override;
	void			OnDeviceDestroy(BOOL bKeepTextures) override;
	void			ValidateHW	() override;
	void			DestroyHW	() override;
	void			Reset		(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2) override;
	void			SetupStates	() override;
	void			OnDeviceCreate(LPCSTR shName) override;
	void			Create		(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2, bool) override;
	void			SetupGPU	(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF) override;
	void			overdrawBegin() override;
	void			overdrawEnd	() override;
	void			DeferredLoad	(BOOL E) override;
	void			ResourcesDeferredUpload() override;
	void			ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps) override;
	void			ResourcesDestroyNecessaryTextures() override;
	void			ResourcesStoreNecessaryTextures() override;
	void			ResourcesDumpMemoryUsage() override;
	void			RenderPrefetchUITextures() override;
	bool			HWSupportsShaderYUV2RGB() override;
	DeviceState		GetDeviceState() override;
	BOOL			GetForceGPU_REF() override;
	u32				GetCacheStatPolys() override;
	void			Begin		() override;
	void			Clear		() override;
	void			End			() override;
	void			ClearTarget	() override;
	void			SetCacheXform(Fmatrix& mView, Fmatrix& mProject) override;
	void			OnAssetsChanged() override;
	IResourceManager* GetResourceManager() const override;

public:
	bgfxResourceManagerLite*	Resources;
	ref_shader					m_WireShader;
	ref_shader					m_SelectionShader;
};

#endif	//	dxRenderDeviceRender_included