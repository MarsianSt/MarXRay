$input v_texcoord0
#include <bgfx_shader.sh>

// Sun shadow-map depth, the bgfx twin of shadow_direct_base.ps:
//
//   #ifdef  USE_HWSMAP
//       return 0;                    <- AXR R4 default: the value lives in the
//   #else                            D32F_LOCKABLE depth texture (r4.cpp:221)
//       return I.depth;              <- the R2/R3 no-HW branch, written into
//   #endif                              r2_RT_smap_surf (D3DFMT_R32F,
//                                        r4_rendertarget.cpp:698-699)
//
// bgfx exposes no comparison sampler on this backend (BGFX_SAMPLER_COMPARE_*
// is absent from the bundled bgfx), so the port takes the reference's own
// non-HW branch: the depth goes into a colour target as a float and the compare
// moves into deferred_light_ps.sc, where it reproduces SampleCmpLevelZero
// (shadow.h:211, :224) term for term - D3D gl_FragCoord.z is the rasteriser's
// z/w, i.e. exactly what a D32F shadow map stores, and the [0,1] depth range
// matches the tc.z that m_shadow's m_TexelAdjust produces
// (r4_rendertarget_accum_direct.cpp:156-162).
//
// R is written as well so the target can stay RGBA16F-free R32F; only .r is read.
void main()
{
    gl_FragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
