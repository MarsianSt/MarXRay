#pragma once
#include "..\..\Include\xrRender\EnvironmentRender.h"
#include "bgfx_capi.h"

class bgfxEnvDescriptorRender : public IEnvDescriptorRender
{
public:
    virtual void Copy(IEnvDescriptorRender &_in) override;
    virtual void OnDeviceCreate(CEnvDescriptor &owner) override;
    virtual void OnDeviceDestroy() override;
    virtual void OnPrepare(CEnvDescriptor& owner) override;
    virtual void OnUnload(CEnvDescriptor& owner) override;

    bgfx_texture_handle_t sky_texture = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t sky_texture_env = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t clouds_texture = BGFX_INVALID_HANDLE;
    bool b_textures_loaded = false;
};

class bgfxEnvDescriptorMixerRender : public IEnvDescriptorMixerRender
{
public:
    virtual void Copy(IEnvDescriptorMixerRender &_in) override;
    virtual void Destroy() override;
    virtual void Clear() override;
    virtual void lerp(IEnvDescriptorRender *inA, IEnvDescriptorRender *inB) override;

    bgfx_texture_handle_t sky_a = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t sky_b = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t sky_env_a = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t sky_env_b = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t clouds_a = BGFX_INVALID_HANDLE;
    bgfx_texture_handle_t clouds_b = BGFX_INVALID_HANDLE;
};

class bgfxEnvironmentRender : public IEnvironmentRender
{
public:
    virtual void Copy(IEnvironmentRender &_in) override;
    virtual void OnFrame(CEnvironment &env) override;
    virtual void OnLoad() override;
    virtual void OnUnload() override;
    virtual void RenderSky(CEnvironment &env) override;
    virtual void RenderClouds(CEnvironment &env) override;
    virtual void OnDeviceCreate() override;
    virtual void OnDeviceDestroy() override;
    virtual particles_systems::library_interface const& particles_systems_library() override;
};
