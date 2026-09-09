#pragma once

#include "..\..\Include\xrRender\RenderDeviceRender.h"

class bgfxRenderDeviceRender : public IRenderDeviceRender
{
public:
    bgfxRenderDeviceRender();
    virtual ~bgfxRenderDeviceRender();

    virtual void Copy(IRenderDeviceRender &_in);

    // Gamma correction
    virtual void setGamma(float fGamma);
    virtual void setBrightness(float fGamma);
    virtual void setContrast(float fGamma);
    virtual void updateGamma();

    // Destroy
    virtual void OnDeviceDestroy(BOOL bKeepTextures);
    virtual void ValidateHW();
    virtual void DestroyHW();
    virtual void Reset(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2);

    // Init
    virtual void SetupStates();
    virtual void OnDeviceCreate(LPCSTR shName);
    virtual void Create(HWND hWnd, u32 &dwWidth, u32 &dwHeight, float &fWidth_2, float &fHeight_2, bool);
    virtual void SetupGPU(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF);

    // Overdraw
    virtual void overdrawBegin();
    virtual void overdrawEnd();

    // Resources control
    virtual void DeferredLoad(BOOL E);
    virtual void ResourcesDeferredUpload();
    virtual void ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps);
    virtual void ResourcesDestroyNecessaryTextures();
    virtual void ResourcesStoreNecessaryTextures();
    virtual void ResourcesDumpMemoryUsage();
    virtual void RenderPrefetchUITextures();

    // HWSupport
    virtual bool HWSupportsShaderYUV2RGB();

    // Device state
    virtual DeviceState GetDeviceState();
    virtual BOOL GetForceGPU_REF();
    virtual u32 GetCacheStatPolys();
    virtual void Begin();
    virtual void Clear();
    virtual void End();
    virtual void ClearTarget();
    virtual void SetCacheXform(Fmatrix &mView, Fmatrix &mProject);
    virtual void OnAssetsChanged();
    virtual IResourceManager* GetResourceManager() const;

    // Present
    virtual void PresentFrame();

    bool InitBGFX(HWND hWnd, u32 width, u32 height);
    void ShutdownBGFX();

private:
    bool m_bInitialized;
    HWND m_hWnd;
    u32 m_width;
    u32 m_height;
    float m_fGamma;
    float m_fBrightness;
    float m_fContrast;
    BOOL m_bForceGPU_REF;
    DeviceState m_deviceState;
};
