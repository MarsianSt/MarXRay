#include "stdafx.h"
#include "bgfxEnvironmentRender.h"

// bgfxEnvDescriptorRender
void bgfxEnvDescriptorRender::Copy(IEnvDescriptorRender &_in) {}
void bgfxEnvDescriptorRender::OnDeviceCreate(CEnvDescriptor &owner) {}
void bgfxEnvDescriptorRender::OnDeviceDestroy() {}
void bgfxEnvDescriptorRender::OnPrepare(CEnvDescriptor& owner) {}
void bgfxEnvDescriptorRender::OnUnload(CEnvDescriptor& owner) {}

// bgfxEnvDescriptorMixerRender
void bgfxEnvDescriptorMixerRender::Copy(IEnvDescriptorMixerRender &_in) {}
void bgfxEnvDescriptorMixerRender::Destroy() {}
void bgfxEnvDescriptorMixerRender::Clear() {}
void bgfxEnvDescriptorMixerRender::lerp(IEnvDescriptorRender *inA, IEnvDescriptorRender *inB) {}

// bgfxEnvironmentRender
void bgfxEnvironmentRender::Copy(IEnvironmentRender &_in) {}
void bgfxEnvironmentRender::OnFrame(CEnvironment &env) {}
void bgfxEnvironmentRender::OnLoad() {}
void bgfxEnvironmentRender::OnUnload() {}
void bgfxEnvironmentRender::RenderSky(CEnvironment &env) {}
void bgfxEnvironmentRender::RenderClouds(CEnvironment &env) {}
void bgfxEnvironmentRender::OnDeviceCreate() {}
void bgfxEnvironmentRender::OnDeviceDestroy() {}

namespace particles_systems {
class stub_particle_library : public library_interface {
public:
    virtual PS::CPGDef const* const* particles_group_begin() const override { return nullptr; }
    virtual PS::CPGDef const* const* particles_group_end() const override { return nullptr; }
    virtual void particles_group_next(PS::CPGDef const* const*& iterator) const override {}
    virtual shared_str const& particles_group_id(PS::CPGDef const& particles_group) const override {
        static shared_str empty;
        return empty;
    }
};
}

particles_systems::library_interface const& bgfxEnvironmentRender::particles_systems_library()
{
    static particles_systems::stub_particle_library stub;
    return stub;
}
