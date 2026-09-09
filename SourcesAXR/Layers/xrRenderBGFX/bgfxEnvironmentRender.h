#pragma once
#include "..\..\Include\xrRender\EnvironmentRender.h"

class bgfxEnvDescriptorRender : public IEnvDescriptorRender
{
public:
    virtual void Copy(IEnvDescriptorRender &_in) override;
    virtual void OnDeviceCreate(CEnvDescriptor &owner) override;
    virtual void OnDeviceDestroy() override;
    virtual void OnPrepare(CEnvDescriptor& owner) override;
    virtual void OnUnload(CEnvDescriptor& owner) override;
};

class bgfxEnvDescriptorMixerRender : public IEnvDescriptorMixerRender
{
public:
    virtual void Copy(IEnvDescriptorMixerRender &_in) override;
    virtual void Destroy() override;
    virtual void Clear() override;
    virtual void lerp(IEnvDescriptorRender *inA, IEnvDescriptorRender *inB) override;
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
