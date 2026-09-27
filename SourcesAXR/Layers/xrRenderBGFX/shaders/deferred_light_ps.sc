$input v_texcoord0

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

// Anomaly deferred lighting resolve: the single pass that turns the unlit albedo
// the port writes today into the lit HDR image. It is the bgfx stand-in for the
// reference chain accum_sun.ps + hmodel() that combine_1.ps:114-166 sums into
// s_accumulator, with the two terms the reference applies there kept apart:
//
//   sun    plight_infinity()  (lmodel.h:178-188) tinted by Ldynamic_color,
//                                  accumulated by accum_sun.ps:23-35
//   hemi   hmodel()           (hmodel.h:20-151), the ambient half
//
// Both specular halves are now ported 1:1: the sun lobe of compute_lighting
// (lmodel.h:148-173) and hmodel's Amb_BRDF term (hmodel.h:145, the
// EnvGGX/pbr_brdf.h:288-309 arm). The sun shadow term is ported 1:1 too: the
// depth from the sun's point of view, sampled with the reference's PCSS kernel
// (shadow.h:169-231) at ps_r_sun_quality = 1, i.e. 8 poisson taps and no
// blocker search. The SSAO factor (combine_1.ps:128-159, computed by
// ssao_calc_ps.sc out of the same G-buffer) is applied 1:1 at the reference's
// own application point, combine_1.ps:183 `hdiffuse *= occ`.

// Attachment 0 of the scene FB: Anomaly f_deffer::C, rgb = albedo + a = gloss
// (gbuffer_stage.h:8, written by deffer_base_flat.ps:54 as float4(D.rgb, def_gloss)
// with D = tbase(), i.e. the albedo is still gamma space).
SAMPLER2D(s_diffuse, 0);
// Attachment 1: view-space position, the same value gbuffer_load_data() rebuilds
// gbd.P from (gbuffer_stage.h:128).
SAMPLER2D(s_position, 1);
// Attachment 2: Anomaly f_deffer::position (gbuffer_stage.h:7) = packed normal .xy,
// view-space z, hemi. The port's decoder stage, see gbuf_pack.h:28-34, :80-83.
SAMPLER2D(s_gbuf, 2);
// env_s0 / env_s1 (hmodel.h:14-15), the ambient cube pair. Sampler stages 3 and 4,
// i.e. right after the three G-buffer attachments. The textures are
// CEnvDescriptor::sky_texture_env of the two descriptors the weather mixer blends
// (Environment_misc.cpp:417-421 -> :819-822 -> dxEnvironmentRender.cpp:151-154;
// bgfx twin: bgfxEnvironmentRender.cpp:422, :464-465, reached through
// bgfxGetAmbientCube()).
SAMPLERCUBE(s_env0, 3);
SAMPLERCUBE(s_env1, 4);
// s_smap: the sun shadow map. game_unpacked/shaders/r3/shadow.h:9 binds it at
// ps t0 under the same name; here it is sampler stage 5, right after the two
// ambient cubes, and it carries the light-space depth of the nearest caster
// rather than a hardware depth texture - see shadow_ps.sc for why. The
// comparison the reference asks the hardware for (SampleCmpLevelZero, shadow.h:
// 48/:211/:224) is done by hand in shadow_smap_test() below.
SAMPLER2D(s_smap, 5);
// s_occ: the screen-space occlusion, the target CRenderTarget::phase_ssao
// writes (r4_rendertarget_phase_ssao.cpp:12-105) and blender_combine.cpp:42
// binds to the combine pass as s_occ = r2_RT_ssao_temp. Sampler stage 6, after
// the shadow map. Half resolution (the reference renders into a viewport of
// dwWidth/2 x dwHeight/2, :52-55), so a full-resolution tc reproduces the
// reference's 2x magnification of the half-res buffer; the sampler is point
// sampled, which is the smp_nofilter the reference reads s_occ with
// (combine_1.ps:133). When the pass could not run, u_cubeValid-style fallback:
// the value is 1, i.e. no occlusion, which is the constant the reference kernel
// converges to for an unoccluded surface.
SAMPLER2D(s_occ, 6);

// Environment sun, fed from CEnvironment::CurrentEnv by bgfxHDR.
//   u_sunDir   normalize(mView * CEnvDescriptor::sun_dir) - already view space,
//              the same vector Ldynamic_dir is (Ldynamic_dir is view space too).
//   u_sunColor CEnvDescriptor::sun_color
//   u_hemiColor CEnvDescriptor::hemi_color, i.e. L_hemi_color
//   u_ambient  CEnvDescriptor::ambient, i.e. L_ambient, with .w =
//              CEnvDescriptorMixer::weight, i.e. env_color.w: the factor the two
//              ambient cubes are lerped with (hmodel.h:105)
uniform vec4 u_sunDir;
uniform vec4 u_sunColor;
uniform vec4 u_hemiColor;
uniform vec4 u_ambient;
// 1 while both env cubes are bound, 0 on a level without an env cube. Then the
// cube term is the constant 1, i.e. the pre-cube behaviour, because a sampler
// with no texture behind it has no defined value (bgfxHDR.cpp, BindAmbientCube).
uniform vec4 u_cubeValid;
// m_shadow of shadow.h:327, the world -> shadow-map matrix. The reference builds
// it once per frame in the accumulator (r4_rendertarget_accum_direct.cpp:152-180)
// as
//   m_shadow = m_TexelAdjust * fuckingsun->X.D.combine * inverse(Device.mView)
//   m_TexelAdjust = { 0.5, 0, 0, 0 / 0, -0.5, 0, 0 / 0, 0, fRange, 0 / 0.5, 0.5, fBias, 1 }
// so the .xy of the result already are shadow-map texcoords, the .z/.w pair is the
// depth the comparison runs against, and the fBias term is the whole depth bias
// the reference applies (ps_r2_sun_depth_far_bias). Because the multiply is
// already done on the CPU, the shader has to undo nothing: it only applies
// tc.xyz /= tc.w (accum_sun_near.ps:70 followed by shadow.h:176).
// G-buffer P is view space here, so the world-space position the reference feeds
// m_shadow with (gbd.P, gbuffer_stage.h:59) is rebuilt through u_invView first.
uniform mat4 u_shadowMat;
//   .x  ps_r2_smapsize, the shadow map edge in texels. AXR compiles it into every
//       shadow shader as SMAP_size (common_defines.h:15-16, r4.cpp:1066); here it
//       comes in as a uniform so the C++ side and the kernel radius cannot drift
//       apart when r2_smap_size is changed.
//   .y  1 while a shadow map was rendered and bound this frame, 0 otherwise. The
//       resolve then keeps the reference's no-map behaviour, i.e. s = 1
//       (accum_sun.ps:26-32 with an unbound s_smap).
uniform vec4 u_shadowParams;

// u_invView is the bgfx predefined per-view uniform (bgfx_shader.sh:836), the
// mirror of the AXR m_inv_V that hmodel.h:48 multiplies the normal with; the
// resolve view is fed Device.mView (bgfxHDR.cpp, ResolvePass) for exactly that.

// game_unpacked/shaders/r3/srgb.h:7-53, same pair combine_ps.sc uses.
float SRGBToLinearF(float x) { return pow(max(0.0, x), 2.2); }
vec3 SRGBToLinearC(vec3 x) { return vec3(SRGBToLinearF(x.r), SRGBToLinearF(x.g), SRGBToLinearF(x.b)); }
float LinearTosRGBF(float x) { return pow(max(0.0, x), 0.45454545); }
vec3 LinearTosRGB(vec3 x) { return vec3(LinearTosRGBF(x.r), LinearTosRGBF(x.g), LinearTosRGBF(x.b)); }

// game_unpacked/shaders/r3/common_functions.h:40-47 compute_colored_ao, the
// albedo-aware occlusion shaping combine_1.ps:156 applies to the SSAO factor
// before the hemisphere term is scaled by it (the Activision "multiplicative
// occlusion" polynomial). HLSL float3 out param -> explicit return.
vec3 compute_colored_ao(float ao, vec3 albedo)
{
    vec3 a = 2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c = 2.7552 * albedo + 0.6903;

    return max(ao, ((ao * a + b) * ao + c) * ao);
}

// xmaterial with USE_R2_STATIC_SUN (common.h:17-18) = float(1.0h/4.h). The port
// keeps the material id as a per-program constant instead of packing it into the
// G-buffer (gbuf_pack.h:85-95) because the bit-packed slot does not survive an
// RGBA16F attachment, so the resolve resolves every pixel with the static value
// the reference uses for the same draw classes.
const float GBUF_MTL = 0.25;

// hmodel.h:9 CUBE_MIPS - the mip the reference asks the sky cube for. The env
// cube is <sky_texture>#small.dds, a single-level 32x32 DXT1 cube (no mip chain
// ships with it), so D3D11 clamps the request to level 0, which is what the
// reference samples too.
const float CUBE_MIPS = 6.0;
// hmodel.h:81 Epsilon, the offset that keeps the three axis taps off the cube
// face borders.
const float CUBE_EPSILON = 0.001;

// hmodel.h:56-61 + :74 - the "fake remap" of the lookup direction: divide by the
// largest component (so the direction lands inside the face that dominates it),
// fold the y component around 0.999 so the top of the cube is not clamped, then
// normalise again.
vec3 cube_remap(vec3 n)
{
    vec3 r = n;
    r /= max(max(abs(r.x), max(abs(r.y), abs(r.z))), 1e-6);
    if (r.y < 0.999)
        r.y = r.y * 2.0 - 1.0;
    return normalize(r);
}

// hmodel.h:80-95 - Valve style ambient cube, ported 1:1. Three taps along the
// axes, each weighted by the squared normal component, instead of one tap along
// the normal: the weights sum to 1 for a unit normal and the blend hides the
// face seams a single tap would show. e0d (hmodel.h:84-88) and e1d (:92-96) are
// the same expression over env_s0 and env_s1; the caller lerps them.
vec3 sampleAmbientCube0(vec3 n)
{
    vec3 nSq = n * n;
    vec3 r = cube_remap(n);
    vec3 e0d = vec3(0.0);
    e0d += nSq.x * textureCubeLod(s_env0, vec3(r.x, CUBE_EPSILON, CUBE_EPSILON), CUBE_MIPS).rgb;
    e0d += nSq.y * textureCubeLod(s_env0, vec3(CUBE_EPSILON, r.y, CUBE_EPSILON), CUBE_MIPS).rgb;
    e0d += nSq.z * textureCubeLod(s_env0, vec3(CUBE_EPSILON, CUBE_EPSILON, r.z), CUBE_MIPS).rgb;
    return e0d;
}

vec3 sampleAmbientCube1(vec3 n)
{
    vec3 nSq = n * n;
    vec3 r = cube_remap(n);
    vec3 e1d = vec3(0.0);
    e1d += nSq.x * textureCubeLod(s_env1, vec3(r.x, CUBE_EPSILON, CUBE_EPSILON), CUBE_MIPS).rgb;
    e1d += nSq.y * textureCubeLod(s_env1, vec3(CUBE_EPSILON, r.y, CUBE_EPSILON), CUBE_MIPS).rgb;
    e1d += nSq.z * textureCubeLod(s_env1, vec3(CUBE_EPSILON, CUBE_EPSILON, r.z), CUBE_MIPS).rgb;
    return e1d;
}

// hmodel.h:101-102, the specular arm of the same cube pair. The two lobes share
// the texture and the remap; only the mip and the single tap along the
// reflection vector differ from the diffuse arm above.
vec3 sampleSpecCube0(vec3 dir, float mip) { return textureCubeLod(s_env0, dir, mip).rgb; }
vec3 sampleSpecCube1(vec3 dir, float mip) { return textureCubeLod(s_env1, dir, mip).rgb; }

// ---------------------------------------------------------------- material
// The reference that runs is the non-ES_PSEUDO_PBR branch (lmodel.h:110-176,
// hmodel.h:20-151): nothing in the r3 tree defines ES_PSEUDO_PBR, so the
// Lit_BRDF() call of lmodel.h:31 is not the code in play, and pbr_settings.h
// turns on USE_BURLEY_DIFFUSE / USE_GGX_SPECULAR for the parts that do run.
//
// Why no material LUT is bound here: s_material (common_samplers.h:69) is only
// sampled at lmodel.h:133, and that result is dead on this branch - the
// reference overwrites light.rgb at lmodel.h:147 and light.w at lmodel.h:151
// before either is read. Every term that reaches the output is the closed form
// of lmodel.h:142-155, driven only by mat_id, so a LUT texture would have
// nothing left to contribute. hmodel.h:114 has the mirror-image line commented
// out. There is therefore no 3D texture to create in bgfxHDR.cpp for this
// feature, and none is invented.

// common_cbuffers.h:8 - Ldynamic_color, the dynamic light cbuffer. Only its
// alpha reaches the shading: lmodel.h:116 (spec *= Ldynamic_color.w) and
// pbr_brdf.h:116 (calc_rough, through 1 - Ldynamic_color.w). The reference
// binder feeds it the sun colour, whose alpha has no port-side counterpart -
// CEnvDescriptor::sun_color is a Fvector3 (Environment.h:186), and the only
// R4 path that would bind it, R_hemi::set_material, is never called
// (R_Backend_hemi.cpp). 1.0 is the reference's own value: the identical
// multiply appears a second time, commented out, at lmodel.h:122. It is a
// constant, not a strength knob - lmodel.h:116 with 0.0 would zero the entire
// specular term, and the reference plainly does not mean that.
const float L_DYNAMIC_W = 1.0;

// common_defines.h:6 - def_gloss, the gloss every G-buffer writer packs into
// the diffuse alpha: deffer_base_flat.ps:51/:54 (static, terrain, decals),
// deffer_grass.ps:88, deffer_particle.ps:69, lod.ps:105. A literal in all of
// them - not r2_gloss_min, not a texture, no per-vertex data. gbuffer_load_data
// reads it back as gbd.gloss (gbuffer_stage.h:140) and that is the alb_gloss.w
// both halves below spend. The port's attachment 0 holds the sampled texel
// verbatim (world_solid_ps.sc:25), i.e. stores no gloss channel at all, so
// DEF_GLOSS is the reference value itself and not a stand-in for missing data.
const float DEF_GLOSS = 2.0 / 255.0;

// anomaly_shaders.h:9
const vec3 LUMINANCE = vec3(0.2125, 0.7154, 0.0721);
// common_brdf.h:16-17
const float MAT_FLORA = 0.15;
const float MAT_FLORA_ELIPSON = 0.04;
// pbr_settings.h:14-30 (ENCHANTED_SHADERS_ENABLED is not defined, so
// ALBEDO_BOOST is 0.5). METALNESS_SOFTNESS is absent on purpose: the reference
// only uses it in commented-out lines (pbr_brdf.h:31, :38).
const float ALBEDO_BOOST = 0.5;
const float ROUGHNESS_LOW = 0.5;
const float ROUGHNESS_HIGH = 1.0;
const float ROUGHNESS_POW = 1.0;
const float SPECULAR_BASE = 0.01;
const float SPECULAR_RANGE = 1.0;
const float SPECULAR_POW = 1.0;
const float METAL_BOOST = 0.25;
const float METALNESS_THRESHOLD = 0.125;
const float PI = 3.14159265359;

// pbr_brdf.h:43-46 Soft_Light, the ternary of the reference spelled as a mix.
vec3 Soft_Light(vec3 base, vec3 blend)
{
    vec3 lo = base - (1.0 - 2.0 * blend) * base * (1.0 - base);
    vec3 hi = base + (2.0 * blend - 1.0) * (sqrt(base) - base);
    return mix(hi, lo, step(blend, vec3(0.5)));
}

// pbr_brdf.h:20-41 calc_metalness. The reference's `? 1 : 0` is step() and its
// METALNESS_SOFTNESS constant is unused (it is commented out there too).
float calc_metalness(vec4 alb_gloss, float material_ID)
{
    float metallerp = max(0.0, (material_ID * 4.0) - 0.5) / 4.0;
    float metalness = step(0.0, saturate(material_ID - 0.75 - 0.001));
    float metal_thres = pow(METALNESS_THRESHOLD, exp2(metallerp));
    float metal_soft = metal_thres * 0.9;
    metalness *= saturate((alb_gloss.a - (metal_thres - metal_soft)) /
                          ((metal_thres + metal_soft) - (metal_thres - metal_soft)));
    return metalness;
}

// pbr_brdf.h:50-54, needed only by calc_specular's metal arm below.
vec3 calc_albedo_boost(vec3 albedo)
{
    vec3 blend = lerp(vec3(0.5), 1.0 - dot(albedo, LUMINANCE), ALBEDO_BOOST);
    return Soft_Light(albedo, blend);
}

// pbr_brdf.h:78-108 calc_specular, complete. calc_albedo (pbr_brdf.h:56-76) is
// deliberately not ported: it feeds the ambient *diffuse* lobe, which the
// resolve already takes from SRGBToLinearC(D.rgb) exactly as lmodel.h:119 does
// for the sun, and swapping it is a different change from the specular port.
vec3 calc_specular(vec4 alb_gloss, float material_ID)
{
    float metalness = calc_metalness(alb_gloss, material_ID);

    vec3 specular = vec3(SPECULAR_BASE);
    vec3 specular_metal = calc_albedo_boost(alb_gloss.rgb);
    specular_metal = SRGBToLinearC(specular_metal);

    material_ID = saturate(material_ID * 1.425);
    alb_gloss.a = sqrt(alb_gloss.a);

    float specular_boost = (material_ID * 2.0 - 1.0) + (alb_gloss.a * 2.0 - 1.0);
    specular_boost = exp2(SPECULAR_RANGE * specular_boost);
    specular_boost = pow(specular_boost, SPECULAR_POW);

    specular *= specular_boost;

    return saturate(lerp(specular, specular_metal, metalness));
}

// pbr_brdf.h:110-122 calc_rough, the roughness the ambient cube is filtered
// with (hmodel.h:39, :44, :78) and the roughness Amb_BRDF integrates against.
float calc_rough(vec4 alb_gloss, float material_ID)
{
    float metalness = calc_metalness(alb_gloss, material_ID);
    alb_gloss.a = pow(alb_gloss.a, ROUGHNESS_POW - (metalness * METAL_BOOST));
    float roughpow = 0.5 / max(0.001, 1.0 - L_DYNAMIC_W);
    float rough = pow(lerp(ROUGHNESS_HIGH, ROUGHNESS_LOW, alb_gloss.a), roughpow);
    return saturate(rough * rough);
}

// pbr_brdf.h:176-182, the DICE roughness blend of the reflection vector back
// toward the normal - it is what keeps a rough surface from sampling a mirror
// direction in the cube.
vec3 getSpecularDominantDir(vec3 N, vec3 R, float roughness)
{
    float smoothness = saturate(1.0 - roughness);
    float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    return lerp(N, R, lerpFactor);
}

// pbr_brdf_ggx.h:58-66 EnvBRDFApprox, the UE4 fit pbr_brdf.h:281 selects
// through USE_GGX_SPECULAR.
vec2 EnvBRDFApprox(float Roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = Roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// pbr_brdf_ggx.h:110-124 EnvGGX, the Amb_Specular of pbr_brdf.h:279-286.
vec3 EnvGGX(vec3 f0, float rough, float nDotV)
{
    vec3 f90Atten = saturate(50.0 * f0);
    vec2 AB = EnvBRDFApprox(rough, nDotV);
    return (f0 * AB.x + AB.y * f90Atten);
}

// pbr_brdf.h:262-268 EnvBurley, the Amb_Diffuse of pbr_brdf.h:270-277 through
// USE_BURLEY_DIFFUSE.
float EnvBurley(float roughness, float NV)
{
    float d0 = 0.97619 - 0.488095 * pow(1.0 - NV, 5.0);
    float d1 = 1.55754 + (-2.02221 + (2.56283 - 1.06244 * NV) * NV) * NV;
    return lerp(d0, d1, roughness);
}

// pbr_brdf.h:288-309 Amb_BRDF, ported whole (albedo arm included) so it can be
// diffed against the reference line by line. hmodel.h:145 passes the literal 0
// as albedo, so the diffuse arm of line :294-295 is multiplied by zero and the
// result is the specular arm alone - which is exactly why hmodel.h:147 has to
// add env_d*albedo back separately.
vec3 Amb_BRDF(float rough, vec3 albedo, vec3 f0, vec3 env_d, vec3 env_s, vec3 V, vec3 N)
{
    float DotNV = dot(N, V);
    float nDotV = max(1e-5, DotNV);

    vec3 diffuse_term = vec3(EnvBurley(rough, nDotV));
    diffuse_term *= env_d * albedo;

    vec3 specular_term = EnvGGX(f0, rough, nDotV);
    specular_term *= env_s;

    float horizon = saturate(DotNV * 2.0);
    horizon *= horizon;
    specular_term *= horizon;

    return diffuse_term + specular_term;
}

// ---------------------------------------------------------------- shadow term
// The sun shadow of accum_sun_near.ps:69-72
//     float4 P4 = float4(_P.xyz + NormalOffset, 1.0);
//     float4 PS = mul(m_shadow, P4);
//     float  s  = sunmask(P4);
//             s *= shadow(PS);
//     return float4(Ldynamic_color * light * s);
// with the NormalOffset term of SSFX_SHADOWS left out (it needs
// ssfx_shadow_bias, a screen-space-shader uniform this port does not carry) and
// sunmask() at its reference default of 1 (shadow.h:324 - USE_SUNMASK off, so
// the clouds mask is not multiplied in).
//
// shadow() is shadow_pcss() here: ps_r_sun_quality defaults to 1
// (xrRender_console.cpp:108), so SUN_QUALITY = 1, USE_ULTRA_SHADOWS stays off
// (accum_sun_near.ps:8-10) and shadow.h:283-294 takes the plain shadow_pcss().
// The SUN_QUALITY <= 3 arm of shadow_pcss (shadow.h:215-228) is therefore the
// one that runs, in full:
//
//     float fRatio = 4.0f / float(SMAP_size);
//     for (i < PCSS_NUM_SAMPLES)  s += SampleCmpLevelZero(smp_smap,
//                                tc.xy + poissonDisk[i]*fRatio, tc.z).x;
//     return s / PCSS_NUM_SAMPLES;
//
// PCSS_NUM_SAMPLES = 8 at SUN_QUALITY 1 (shadow.h:159-160). The blocker search
// of the SUN_QUALITY > 3 arm is not compiled in the reference at this quality
// either, so nothing of that arm is missing.
//
// The one substitution: SampleCmpLevelZero with the D3D comparison state
// LESS_EQUAL returns 1 when the reference depth is <= the stored depth, which is
// what shadow_smap_test() spells out. That is the whole of the port's
// depth-format deviation - see shadow_ps.sc.

// shadow.h:121-154, poissonDisk[0..7] - the first PCSS_NUM_SAMPLES entries, which
// are the only ones the SUN_QUALITY 1 arm reads. The values are verbatim. They are
// spelled out as eight named constants instead of a table because this file is
// compiled through the HLSL parser (bgfxShaderCompiler.cpp:47-50 selects the
// spirv profile on the Vulkan backend, and shaderc routes it through HLSL), which
// rejects the GLSL array-constructor initialiser. Same eight taps, same order.
const vec2 POISSON_0 = vec2(0.0617981, 0.07294159);
const vec2 POISSON_1 = vec2(0.6470215, 0.7474022);
const vec2 POISSON_2 = vec2(-0.5987766, -0.7512833);
const vec2 POISSON_3 = vec2(-0.693034, 0.6913887);
const vec2 POISSON_4 = vec2(0.6987045, -0.6843052);
const vec2 POISSON_5 = vec2(-0.9402866, 0.04474335);
const vec2 POISSON_6 = vec2(0.8934509, 0.07369385);
const vec2 POISSON_7 = vec2(0.1592735, -0.9686295);

// shadow.h:237-242, `test` without the reference's tc.xyz /= tc.w (done once by
// the caller, shadow.h:176) and with SampleCmpLevelZero in its spelled-out form:
// the D3D comparison the reference asks for is LESS_EQUAL against the stored
// depth, so the tap contributes 1 (lit) exactly when ref <= stored.
float shadow_smap_test(vec2 tc, float ref)
{
    return step(texture2D(s_smap, tc).r, ref);
}

// shadow.h:215-228, the no-blocker-search arm, with the loop written out:
// PCSS_NUM_SAMPLES = 8 (shadow.h:159-160, SUN_QUALITY 1) and
// fRatio = 4.0f / float(SMAP_size).
float shadow_smap_pcss(vec3 tc)
{
    float fRatio = 4.0 / u_shadowParams.x;
    float s = 0.0;
    s += shadow_smap_test(tc.xy + POISSON_0 * fRatio, tc.z);
    s += shadow_smap_test(tc.xy + POISSON_1 * fRatio, tc.z);
    s += shadow_smap_test(tc.xy + POISSON_2 * fRatio, tc.z);
    s += shadow_smap_test(tc.xy + POISSON_3 * fRatio, tc.z);
    s += shadow_smap_test(tc.xy + POISSON_4 * fRatio, tc.z);
    s += shadow_smap_test(tc.xy + POISSON_5 * fRatio, tc.z);
    s += shadow_smap_test(tc.xy + POISSON_6 * fRatio, tc.z);
    s += shadow_smap_test(tc.xy + POISSON_7 * fRatio, tc.z);
    return s * 0.125;   // s / float(PCSS_NUM_SAMPLES)
}

void main()
{
    vec2 tc = v_texcoord0;
    vec4 D = texture2D(s_diffuse, tc);
    vec3 P = texture2D(s_position, tc).xyz;
    vec4 G = texture2D(s_gbuf, tc);

    // Nothing wrote the position G-buffer here, i.e. this is sky or cloud: they
    // only ever target attachment 0, so attachment 1 keeps the clear value and
    // P stays 0 (see bgfxHDR::BindScene). Their radiance is already final, so
    // the reference never lights them either - the AXR sky is resolved by the
    // combine's sky blend (combine_1.ps:204) on top of the fogged scene.
    if (dot(P, P) < 1e-6)
    {
        gl_FragColor = vec4(D.rgb, 1.0);
        return;
    }

    // gbuffer_stage.h:59-65 / :83-86, the reference unpack stage.
    vec3 N = gbuf_unpack_normal(G.xy);
    float hemi = gbuf_unpack_hemi(G.w);

    // combine_1.ps:128-159, the SSAO member of the combine. In the reference the
    // factor is either computed inline by calc_ssao() (combine_1.ps:148) or read
    // out of the buffer phase_ssao wrote (blender_combine.cpp:42), and either way
    // it is the same kernel; this port runs the separate pass (ssao_calc_ps.sc)
    // and samples it here, because the ambient it multiplies is built below in
    // this pass and not in the bgfx combine (which is post-tonemap only).
    // combine_1.ps:156 runs it through compute_colored_ao with the *raw* G-buffer
    // albedo (D.xyz, still gamma space - combine_1.ps:88), not the linearised
    // one, and only under SSAO_QUALITY, which the reference build always defines
    // (r4.cpp:1365-1371, ps_r_ssao = 3).
    vec3 occ = compute_colored_ao(texture2D(s_occ, tc).x, D.rgb);

    // lmodel.h:118-120 - compute_lighting linearises the G-buffer albedo before
    // it modulates anything, and combine_1.ps:161 gamma-corrects D.rgb the same
    // way for the ambient stored in the accumulator alpha.
    vec3 albedo = SRGBToLinearC(D.rgb);

    // The G-buffer diffuse the reference hands to both halves as alb_gloss. Its
    // alpha is the gloss channel, which the port does not store (see DEF_GLOSS),
    // so it is filled with the reference's own literal.
    vec4 alb_gloss = vec4(D.rgb, DEF_GLOSS);

    // lmodel.h:113-120 - the sun's specular mask: the gloss channel scaled by
    // the dynamic light alpha, then linearised. hmodel.h:143 spends the same
    // gloss without the Ldynamic_color.w factor, so the ambient keeps its own
    // copy below rather than sharing this one.
    float specSun = SRGBToLinearF(DEF_GLOSS * L_DYNAMIC_W);

    // ----- sun: accum_sun.ps:23-35
    // plight_infinity (lmodel.h:180-183): L = -normalize(light_direction) with
    // light_direction = Ldynamic_dir, which the port's u_sunDir already is in
    // view space; V = -normalize(pnt) with pnt the view-space position.
    vec3 L = -normalize(u_sunDir.xyz);
    vec3 V = -normalize(P);
    float NdotL = saturate(dot(N, L));

    // lmodel.h:126-129, the half vector and the two dot products only the
    // specular lobe consumes.
    vec3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float HdotL = saturate(dot(H, L));

    // lmodel.h:142, :146-147 - the non-LUT diffuse of the non-ES_PSEUDO_PBR
    // branch, with the s_material lookup of lmodel.h:133 skipped (dead there,
    // see the material block above):
    //   metalness = ceil(mat_id - 0.75)
    //   gloss     = saturate(mat_id/0.75) - 0.5*metalness
    //   exponent  = ((gloss*0.5)*0.8) + 0.6
    //   light.rgb = pow(NdotL, exponent) * ((exponent + 1.0) * 0.5)
    float metalness = ceil(GBUF_MTL - 0.75);
    float gloss = saturate(GBUF_MTL / 0.75);
    gloss -= 0.5 * metalness;
    float exponent = ((gloss * 0.5) * 0.8) + 0.6;
    float sunDiffuse = pow(NdotL, exponent) * ((exponent + 1.0) * 0.5);

    // lmodel.h:150-151 - the specular lobe. glossiness = exp2(gloss^1.5 * 14),
    // which at gloss = 1/3 is 32, i.e. a narrow Blinn lobe; the (g+2)/8 term is
    // the reference's Blinn normalisation (written `2/8` there) and
    // saturate(4*NdotL) the grazing fade.
    float glossiness = exp2(pow(gloss, 1.5) * 14.0);
    float sunSpecular = pow(NdotH, glossiness) * (glossiness + 2.0 / 8.0) * saturate(4.0 * NdotL);

    // lmodel.h:154-155 - non-PBR fresnel, f0 = lerp(0.04, 0.75, metalness).
    float f0 = lerp(0.04, 0.75, metalness);
    sunSpecular *= lerp(f0, 1.0, pow(1.0 - HdotL, 5.0));

    // lmodel.h:167 - the gloss channel as the specular mask.
    sunSpecular *= specSun;

    // lmodel.h:170 - a metal surface tints its specular by its own diffuse.
    // mat_id = GBUF_MTL is 0.25 here, far below the 0.75 metal threshold, so
    // calc_metalness/ceil() both give 0 and metaltint is the reference's 1.0.
    vec3 metaltint = mix(vec3(1.0), albedo / max(dot(LUMINANCE, albedo), 0.01), metalness);

    // lmodel.h:166, :173 - light.rgb = light.rgb*albedo and
    // light.rgb += light.www*metaltint, i.e. the lobe above enters already
    // masked by the gloss channel.
    vec3 sun = sunDiffuse * albedo + sunSpecular * metaltint;

    // accum_sun.ps:26-32 / accum_sun_near.ps:69-72 - the shadow term s, which
    // multiplies the whole sun lobe.
    //
    // accum_sun.ps:27-30 is the R3 four-quadrant form: it reads the LT/RT/LB/RB
    // varyings the R3 VS emits and takes one channel per quadrant of an RGBA
    // shadow map. That is the older sibling of the same term - it has no m_shadow,
    // no SMAP_size and no PCSS - and the R4 chain this port follows
    // (r2_R_sun.cpp -> phase_smap_direct -> accum_sun_near.ps) produces the
    // 2D shadow map accumulated here instead. So the ported form is the
    // accum_sun_near one, which is the same factor in the same place.
    //
    // gbd.P is world space in AXR (gbuffer_stage.h:59-65); this port's
    // s_position carries the view-space position, so u_invView rebuilds the
    // world-space point the reference hands to m_shadow.
    vec3 Pw = mul(u_invView, vec4(P, 1.0)).xyz;
    vec4 PS = mul(u_shadowMat, vec4(Pw, 1.0));
    // shadow.h:176, tc.xyz /= tc.w, the perspective divide the reference does
    // once for the whole kernel.
    vec3 shadowTc = PS.xyz / PS.w;
    // 1 = the map is bound, 0 = no map this frame. The reference has no such
    // switch: an unbound s_smap is undefined, and accum_sun_near.ps always has
    // one. Here the pass may be unavailable, and 1 is the reference's own
    // unshadowed value (shadow.h:324's sunmask() default, and the constant the
    // resolve used before the map existed).
    float sShadow = (u_shadowParams.y > 0.5) ? shadow_smap_pcss(shadowTc) : 1.0;

    // accum_sun.ps:29-35 result *= light * SRGBToLinear(Ldynamic_color.rgb), i.e.
    // the sun colour multiplies the specular lobe too, and the shadow factor
    // scales the product exactly as accum_sun_near.ps:81 return float4(
    // Ldynamic_color * light * s) does.
    sun *= sShadow * SRGBToLinearC(u_sunColor.rgb);

    // ----- hemi ambient: hmodel.h, the diffuse half of combine_1.ps:166
    //   hmodel.h:47-48  nw = normalize(mul(m_inv_V, normal)), world space, the
    //                   space the ambient cube is oriented in
    //   hmodel.h:80-106 the Valve ambient cube, lerped with env_color.w
    //   hmodel.h:109  hscale = h, the per-pixel hemi out of the G-buffer
    //   hmodel.h:113  light  = hscale (the s_material tint is commented out)
    //   hmodel.h:118  env_col = env_color.rgb, fed the descriptor hemi colour,
    //                   the same source calc_model_hemi_r1() reads
    //                   (common_functions.h:99-101)
    //   hmodel.h:121  env_d *= env_col
    //   hmodel.h:125  env_d *= light.xxx
    //   hmodel.h:130  env_d += L_ambient
    //   hmodel.h:133  env_d  = SRGBToLinear(env_d)
    //   hmodel.h:147  hdiffuse += env_d * albedo
    vec3 nw = mul(u_invView, vec4(N, 0.0)).xyz;
    vec3 cubeD = mix(sampleAmbientCube0(nw), sampleAmbientCube1(nw), u_ambient.w);
    // No cube on this level: fall back to the constant the port used before the
    // ambient cube was bound (the cube replaced by 1).
    cubeD = mix(vec3(1.0), cubeD, u_cubeValid.x);
    vec3 env_d = SRGBToLinearC(cubeD * u_hemiColor.rgb * hemi + u_ambient.rgb);

    // ----- ambient specular: hmodel.h:39-44, :64-78, :101-106, :121-134
    // The reflection direction. combine_1.ps:166 hands hmodel the view-space
    // position, which hmodel.h:52 normalises, so v2Pnt is this pass's P and the
    // m_inv_V multiply of hmodel.h:53 is the same rotate-only view inverse nw
    // went through above.
    vec3 v2Pnt = normalize(P);
    // hmodel.h:64 - reflect(view vector, world normal), then the identical
    // remap + renormalise pair the diffuse taps use (hmodel.h:66-75).
    vec3 vreflect = reflect(v2Pnt, nw);
    vec3 vreflectRemap = normalize(cube_remap(vreflect));

    // hmodel.h:35, :39 - the roughness, and hmodel.h:44 the mip it filters the
    // cube with. calc_rough reads the raw gloss of alb_gloss, not the
    // linearised specSun.
    float roughCube = calc_rough(alb_gloss, GBUF_MTL);
    float roughMip = CUBE_MIPS - ((1.0 - roughCube) * CUBE_MIPS);

    // hmodel.h:78 - blend the reflection vector toward the normal by roughness
    // so a rough surface stops sampling a mirror direction.
    vreflectRemap = getSpecularDominantDir(normalize(cube_remap(nw)), vreflectRemap, roughCube);

    // hmodel.h:101-102, :106 - the same two cubes, one tap along the
    // reflection direction at the roughness mip instead of the three diffuse
    // taps, lerped with env_color.w.
    vec3 cubeS = mix(sampleSpecCube0(vreflectRemap, roughMip),
                     sampleSpecCube1(vreflectRemap, roughMip), u_ambient.w);
    cubeS = mix(vec3(1.0), cubeS, u_cubeValid.x);
    // hmodel.h:121-134 - the same tint / scale / add / linearise chain env_d
    // went through. hmodel.h:113 sets all four components of `light` to hscale
    // (h = the G-buffer hemi) and hmodel.h:126 spends light.www, so env_s is
    // scaled by hscale, not by hspec: hmodel.h:110 computes hspec and
    // hmodel.h:150 then throws it away ("do not use hspec at all").
    vec3 env_s = SRGBToLinearC(cubeS * u_hemiColor.rgb * hemi + u_ambient.rgb);

    // hmodel.h:145 - Amb_BRDF(roughCube, 0, specular, env_d, env_s*!m_flora,
    // -v2Pnt, nw). `env_s * !m_flora` is env_s itself here: hmodel.h:28 tests
    // abs(m - MAT_FLORA) <= MAT_FLORA_ELIPSON and GBUF_MTL is 0.25, not the
    // 0.15 flora id, and the same test at lmodel.h:158 makes the grass SSS a
    // no-op for the static class this resolve lights.
    vec3 f0Ambient = calc_specular(alb_gloss, GBUF_MTL);
    vec3 ambientSpecular = Amb_BRDF(roughCube, 0.0, f0Ambient, env_d, env_s, -v2Pnt, nw);

    // hmodel.h:143, :146 - the ambient's own copy of the gloss channel (no
    // Ldynamic_color.w factor on this side) and the reference's spec*2 gain.
    float specAmbient = SRGBToLinearF(DEF_GLOSS);
    // hmodel.h:145-147 - the BRDF result scaled, then the env_d*albedo the
    // zeroed albedo argument of Amb_BRDF could not supply. env_d*albedo is
    // hdiffuse, the only term combine_1.ps:183 scales (`hdiffuse *= occ`); the
    // `hspecular *= occ` of the next line stays commented out in the reference
    // (combine_1.ps:185), so the ambient specular keeps its unoccluded value -
    // which is why occ multiplies only the first product here.
    vec3 ambient = (env_d * albedo) * occ + ambientSpecular * (specAmbient * 2.0);

    // combine_1.ps:187-188 sums the two halves into the linear accumulator and
    // then gamma-corrects it: color = LinearTosRGB(L.rgb + hdiffuse.rgb). The
    // bgfx post chain expects the same encoding at this point - combine_ps.sc
    // hands the value to tonemap(), whose first act is SRGBToLinear (see the
    // comment on tonemap() there) - so the resolve has to close the same round
    // trip the reference does.
    gl_FragColor = vec4(LinearTosRGB(sun + ambient), 1.0);
}
