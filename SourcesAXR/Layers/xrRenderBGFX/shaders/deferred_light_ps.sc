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
// EnvGGX/pbr_brdf.h:288-309 arm). The dynamic point / spot accumulators are
// ported 1:1 as well: accum_omni_unshadowed.ps and accum_base.ps, i.e. the
// unshadowed elements CRenderTarget::accum_point / accum_spot select
// (r4_rendertarget_accum_point.cpp:97-101, r4_rendertarget_accum_spot.cpp:139-143),
// running the same plight_local (lmodel.h:190-207) over the same volumes the
// reference's du_sphere / du_cone geometry describes. The sun shadow term is
// ported 1:1 too: the
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
//   u_sunSpec  Ldynamic_color.w of the SUN, i.e. u_diffuse2s(sun colour) - the
//              gloss weight lmodel.h:116 and pbr_brdf.h:116 spend (see the
//              L_DYNAMIC_W note below). CEnvDescriptor::sun_color is a
//              Fvector3 (Environment.h:186) and has no .w of its own, so it
//              travels in its own uniform, exactly as the reference carries it
//              in the dynamic_light cbuffer's alpha.
//   u_hemiColor CEnvDescriptor::hemi_color, i.e. L_hemi_color
//   u_ambient  CEnvDescriptor::ambient, i.e. L_ambient, with .w =
//              CEnvDescriptorMixer::weight, i.e. env_color.w: the factor the two
//              ambient cubes are lerped with (hmodel.h:105)
uniform vec4 u_sunDir;
uniform vec4 u_sunColor;
// Ldynamic_color.w for the sun, i.e. L_spec = u_diffuse2s(CEnvDescriptor:
// :sun_color). The reference builds exactly that vector for the combine
// (r4_rendertarget_phase_combine.cpp:245-254, bound at :306) and for the sun
// accumulator (r4_rendertarget_accum_direct.cpp:59-61, bound at :226), and both
// of its consumers read the .w: lmodel.h:116 and pbr_brdf.h:116. CEnvDescriptor:
// :sun_color is a Fvector3 with nothing to put in the fourth slot, so the port
// carries the reference's own alpha in a uniform of its own; see the long note
// at the calc_rough block below.
uniform vec4 u_sunSpec;
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
// G-buffer P is view space here (gbuffer_stage.h:15-17), which is exactly what
// accum_sun_near.ps:70 multiplies m_shadow with - the view->world step is the
// invView that is already inside m_shadow, so it must not be repeated here.
uniform mat4 u_shadowMat;
//   .x  ps_r2_smapsize, the shadow map edge in texels. AXR compiles it into every
//       shadow shader as SMAP_size (common_defines.h:15-16, r4.cpp:1066); here it
//       comes in as a uniform so the C++ side and the kernel radius cannot drift
//       apart when r2_smap_size is changed.
//   .y  1 while a shadow map was rendered and bound this frame, 0 otherwise. The
//       resolve then keeps the reference's no-map behaviour, i.e. s = 1
//       (accum_sun.ps:26-32 with an unbound s_smap).
uniform vec4 u_shadowParams;

// ---------------------------------------------------------------- dynamic lights
// The point / spot accumulators of the reference, 1:1:
//   accum_omni_unshadowed.ps  (POINT, r4_rendertarget_accum_point.cpp:101 SE_L_UNSHADOWED)
//   accum_base.ps             (SPOT,  r4_rendertarget_accum_spot.cpp:143  SE_L_UNSHADOWED)
// Both read the G-buffer at the pixel, run plight_local (lmodel.h:190-207) and add
// Ldynamic_color * light into the same rt_Accumulator the sun writes, which
// combine_1.ps:114-166 sums with hdiffuse. This pass is that accumulator: the three
// terms are summed here, at the same point of the same expression, and closed
// through the same LinearTosRGB.
//
// bgfx has no stencil and no per-light geometry pass, so the volumes are not drawn
// and not masked: they are evaluated per pixel against the exact solids the
// reference's du_sphere / du_cone geometry describes (see the volume test in
// main()). Everything else - the attenuation, the cone solid, the light colour,
// the range factor and the BRDF - is the reference's own expression.
//
// Three vec4 per light, exactly the two vectors the accumulators bind plus the
// cone axis:
//   [0] Ldynamic_pos    (L_pos_view.xyz, 1/(L_R*L_R))   accum_point.cpp:104 / accum_spot.cpp:150
//   [1] Ldynamic_color  (L_clr.rgb, L_spec)              accum_point.cpp:105 / accum_spot.cpp:151
//   [2] the cone axis, view space (accum_spot.cpp:127-129 builds it for the
//       geometry and leaves it commented out, because the cone lives in the
//       vertices; the analytic volume test needs it explicitly)
#define MAX_DYN_LIGHTS 32
uniform vec4 u_lights[MAX_DYN_LIGHTS * 3];
//   .x  1 for IRender_Light::SPOT, 0 for IRender_Light::POINT - the switch
//        light::export_to makes between package.v_spot and package.v_point
//        (light.cpp:364-365), which is the reference's own omni/spot split.
//   .y  tan(cone/2), the very factor light::xform_calc scales the cone with
//        (light.cpp:281, s = 2*range*tanf(cone/2)).
uniform vec4 u_lightParams[MAX_DYN_LIGHTS];
//   .x  number of lights in u_lights / u_lightParams this frame.
uniform vec4 u_lightCount;

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

// The material id is NOT a constant here any more: it is read per pixel out of
// the G-buffer with gbuf_unpack_mtl() (gbuffer_stage.h:88-93, the call
// gbuffer_load_data() makes at :134), i.e. exactly the `float mtl = P.w` of
// combine_1.ps:106. Every writer packs its own class value - 0.25 xmaterial for
// static/decal/skin/particle (common.h:17-18 with USE_R2_STATIC_SUN,
// deffer_base_flat.ps:21, deffer_base_aref_flat.ps:72, deffer_particle.ps:66),
// 0.15 MAT_FLORA for grass (deffer_grass.ps:101), 0.95 for terrain
// (deffer_terrain_mid_flat.ps:57, deffer_terrain_low_flat.ps:24) - so the
// terrain's `m = 0` override of hmodel.h:27-30 and the flora test of lmodel.h:158
// / combine_1.ps:97 are live here exactly as in the reference.
//
// xmaterial itself, common.h:17-18: `float(1.0h/4.h)` under USE_R2_STATIC_SUN,
// which r4.cpp:1150-1153 defines when o.sunstatic is set. The one term that still
// wants the constant rather than the G-buffer value is combine_1.ps:111, the
// sun lobe, which overwrites mtl with it; see the sun block in main().
const float XMATERIAL = 0.25;

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
// pbr_brdf.h:116 (calc_rough, through 1 - Ldynamic_color.w).
//
// That alpha is L_spec = u_diffuse2s(L_clr), the sun colour's own gloss weight -
// r4_rendertarget_accum_direct.cpp:60-61/:226 and
// r4_rendertarget_phase_combine.cpp:247-252/:306 both build Ldynamic_color as
// (L_clr.rgb, u_diffuse2s(L_clr)). u_diffuse2s (r2_types.h:160-172) with its
// method-0 arm and the reference console defaults ps_r2_gloss_min = 0.0f /
// ps_r2_gloss_factor = 0.001f (xrRender_console.cpp:373-374) is
//
//     v   = (r + g + b) / 3
//     out = 0.0 + 0.001 * (v < 1 ? v^(2/3) : v)
//
// i.e. ~1e-3 for any sun colour of magnitude 1, NOT 1.0. It reaches the shading
// twice, and both times hard:
//
//   pbr_brdf.h:116  roughpow = 0.5 / max(0.001, 1 - Ldynamic_color.w)
//                  -> 0.5 / 0.999 ~= 0.5, not 0.5 / max(0.001, 0) = 500.
//                  500 is not a mild difference: pow(lerp(1, 0.5, g), 500)
//                  saturates to 0 for every gloss below 1, so calc_rough
//                  returned 0 (a mirror) everywhere and the whole ambient
//                  specular arm was computed at RoughMip = CUBE_MIPS - CUBE_MIPS
//                  = 0, i.e. the sharpest cube level.
//   lmodel.h:116   spec *= Ldynamic_color.w then :120 SRGBToLinear(spec)
//                  -> the sun's specular lobe is scaled by ~1e-3 before the
//                  gamma, exactly as the reference scales it.
//
// The value is therefore the sun's own u_diffuse2s, fed in as u_sunSpec (the
// .w slot CEnvDescriptor::sun_color has no use for; bgfxHDR feeds 0.0 there
// today). It is the same quantity the accumulators pass as Ldynamic_color.w
// per light, i.e. bgfxHDR's u_diffuse2s() of the light colour.

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
// Ldynamic_color.w is a parameter, not a constant, because in the reference the
// combine binds the SUN into the dynamic-light cbuffer
// (r4_rendertarget_phase_combine.cpp:306) and calc_rough is reached from
// hmodel.h:35 through that same cbuffer - so the exponent it computes is the
// sun's own u_diffuse2s, not a literal. See the note on the constant.
float calc_rough(vec4 alb_gloss, float material_ID, float Ldynamic_w)
{
    float metalness = calc_metalness(alb_gloss, material_ID);
    alb_gloss.a = pow(alb_gloss.a, ROUGHNESS_POW - (metalness * METAL_BOOST));
    float roughpow = 0.5 / max(0.001, 1.0 - Ldynamic_w);
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

// lmodel.h:110-176 compute_lighting, the non-ES_PSEUDO_PBR branch, in full and
// with nothing folded away, so the sun and both accumulators can share it. The two
// inputs the reference reads from globals become arguments:
//   mat_id   `m`, i.e. xmaterial with USE_R2_STATIC_SUN = 1/4 (common.h:17-18)
//            for the sun (combine_1.ps:111) and the per-pixel G-buffer value for
//            the accumulators (combine_1.ps:106)
//   lightW   the Ldynamic_color.w factor of lmodel.h:116 - `spec *= Ldynamic_color.w`
//            then lmodel.h:120 linearises it. The sun passes u_sunSpec, i.e.
//            u_diffuse2s of the sun colour (r4_rendertarget_phase_combine.cpp:247-252,
//            bound at :306); the accumulators pass L_spec, i.e.
//            u_diffuse2s of the light colour (r2_types.h:160-172, bound at
//            r4_rendertarget_accum_point.cpp:37/:105 and accum_spot.cpp:125/:151),
//            so a point light really does scale its own specular the way the
//            reference does.
// Not ported, both for the reason already given on the material block above: the
// s_material lookup at lmodel.h:133 (its result is overwritten at :147 and :151
// before it is read) and the grass SSS at lmodel.h:158-163.
//
// The SSS is left out for a second, harder reason: it calls SSS(), which is not
// in the r3 tree at all - a grep of game_unpacked/shaders/r3 for `float3 SSS`
// finds nothing, and pbr_brdf.h only mentions SSS inside the ES_PSEUDO_PBR
// Lit_BRDF arm, which is not the branch that compiles here. The flora test
// lmodel.h:158 is therefore live (mat_id 0.15 reaches it now) but its body has
// no definition in the reference to port, so it stays out rather than being
// approximated. The test itself is kept below so the reachability is visible.
vec3 compute_lighting_lmodel(vec3 N, vec3 V, vec3 L, vec4 alb_gloss, float mat_id, float lightW)
{
    // lmodel.h:114-120
    vec3 albedo = SRGBToLinearC(alb_gloss.rgb);
    float spec = SRGBToLinearF(alb_gloss.a * lightW);

    // lmodel.h:126-129
    vec3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));
    float HdotL = saturate(dot(H, L));

    // lmodel.h:113, :142-144
    float metalness = ceil(mat_id - 0.75);
    float gloss = saturate(mat_id / 0.75);
    gloss -= 0.5 * metalness;
    float exponent = ((gloss * 0.5) * 0.8) + 0.6;

    // lmodel.h:146-147 - the non-LUT diffuse, over the linearised albedo.
    vec3 diffuse = pow(NdotL, exponent) * ((exponent + 1.0) * 0.5);

    // lmodel.h:150-151 - the Blinn lobe, the reference's own normalisation (written
    // `2/8` there) and the grazing fade.
    float glossiness = exp2(pow(gloss, 1.5) * 14.0);
    float specular = pow(NdotH, glossiness) * (glossiness + 2.0 / 8.0) * saturate(4.0 * NdotL);

    // lmodel.h:154-155 - non-PBR fresnel.
    float f0 = lerp(0.04, 0.75, metalness);
    specular *= lerp(f0, 1.0, pow(1.0 - HdotL, 5.0));

    // lmodel.h:167 - the gloss channel as the specular mask.
    specular *= spec;

    // lmodel.h:170 - a metal surface tints its specular by its own diffuse.
    vec3 metaltint = mix(vec3(1.0), albedo / max(dot(LUMINANCE, albedo), 0.01), metalness);

    // lmodel.h:166, :173
    return diffuse * albedo + specular * metaltint;
}

// lmodel.h:190-207 plight_local, the light model both accumulators run
// (accum_omni_unshadowed.ps:41, accum_base.ps:50). Ldynamic_pos and Ldynamic_color
// are the two vectors the reference binds per light, so they arrive whole:
//   rsqr = dot(L2P, L2P); att = saturate(1 - rsqr * light_range_rsq)
// The .w of Ldynamic_pos is that 1/(L_R*L_R) with L_R = range * 0.95
// (r4_rendertarget_accum_point.cpp:35/:104, r4_rendertarget_accum_spot.cpp:148/:150),
// so the attenuation term needs no reconstruction here.
// The ES_PSEUDO_PBR arm of lmodel.h:56-84 - the one that is not compiled in this
// build - carries an extra rsqr = max(rsqr, 0.1) clamp; the branch that runs does
// not, so it is not here either.
vec3 plight_local(vec3 pnt, vec3 normal, vec4 alb_gloss, vec4 Ldynamic_pos, vec4 Ldynamic_color, float mtl)
{
    vec3 L2P = pnt - Ldynamic_pos.xyz;
    float rsqr = dot(L2P, L2P);

    float att = saturate(1.0 - rsqr * Ldynamic_pos.w);
    att = SRGBToLinearF(att);

    vec3 N = normalize(normal);
    vec3 V = normalize(-pnt);
    vec3 L = normalize(-L2P);

    // accum_omni_unshadowed.ps:50 / accum_base.ps:88: Ldynamic_color * light, with
    // the lightmap (spot) and the shadow (off in both unshadowed elements) at their
    // own reference defaults of 1 - accum_base.ps:69 and :63.
    //
    // The material id is the per-pixel one, the same value combine_1.ps:166 hands
    // hmodel: the accumulators in the reference read `m` from the G-buffer
    // themselves (gbuffer_load_data().mtl, combine_1.ps:106), and only the sun's
    // own call is overridden with the xmaterial constant (combine_1.ps:111).
    return Ldynamic_color.rgb * (att * compute_lighting_lmodel(N, V, L, alb_gloss, mtl, Ldynamic_color.w));
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

    // gbuffer_stage.h:59-65, :83-86 and :88-93 - the reference unpack stage, the
    // three reads gbuffer_load_data() makes at :131/:134/:137. The material id is
    // the `float mtl = P.w` of combine_1.ps:106 and is what hmodel's `m` and
    // plight_infinity's `m` are below; it is per-pixel now, not a constant.
    vec3 N = gbuf_unpack_normal(G.xy);
    float hemi = gbuf_unpack_hemi(G.w);
    float mtl = gbuf_unpack_mtl(G.w);

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

    // The G-buffer diffuse the reference hands to every half below as alb_gloss. Its
    // alpha is the gloss channel, which the port does not store (see DEF_GLOSS), so
    // it is filled with the reference's own literal. lmodel.h:116 then scales it by
    // the light's own Ldynamic_color.w inside compute_lighting_lmodel; hmodel.h:143
    // spends the same gloss without that factor, so the ambient keeps its own copy
    // below (specAmbient) rather than sharing the one the light model makes.
    vec4 alb_gloss = vec4(D.rgb, DEF_GLOSS);

    // ----- sun: accum_sun.ps:23-35
    // plight_infinity (lmodel.h:180-183): L = -normalize(light_direction) with
    // light_direction = Ldynamic_dir, which the port's u_sunDir already is in
    // view space; V = -normalize(pnt) with pnt the view-space position. The whole
    // lobe is lmodel.h:110-176 with Ldynamic_color.w = u_sunSpec.
    vec3 L = -normalize(u_sunDir.xyz);
    vec3 V = -normalize(P);
    // combine_1.ps:111/:116 - `mtl = xmaterial` and the lobe runs on it. The
    // reference overwrites the G-buffer's mtl with the xmaterial constant for
    // this one call (that line is inside `#ifdef USE_R2_STATIC_SUN`), while
    // hmodel at :166 keeps the G-buffer value - so the sun passes the class
    // constant and the ambient passes the per-pixel id, exactly as here. Under
    // USE_R2_STATIC_SUN xmaterial is float(1.0h/4.h) (common.h:17-18), which is
    // what the static/decal writers pack, so for those classes the two are the
    // same number; for terrain (0.95) and grass (0.15) the reference's split is
    // what keeps the sun lobe and the ambient BRDF apart.
    vec3 sun = compute_lighting_lmodel(N, V, L, alb_gloss, XMATERIAL, u_sunSpec.x);

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
    // gbd.P is VIEW space (gbuffer_stage.h:15-17, "float3 P; //View space
    // position") and accum_sun_near.ps:70 applies m_shadow to it directly: the
    // invView that turns it into world space is already inside m_shadow itself
    // (r4_rendertarget_accum_direct.cpp:170-171 builds
    // m_shadow = m_TexelAdjust * combine * xf_invview). Multiplying u_invView
    // here as well would apply it twice and throw the texcoords out of [0,1].
    vec4 PS = mul(u_shadowMat, vec4(P, 1.0));
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

    // ----- dynamic point / spot accumulators
    // r4_R_lights.cpp:191-209 and :248-271 walk package.v_point / package.v_spot
    // and call accum_point / accum_spot, which add their volume into the same
    // rt_Accumulator the sun just wrote; combine_1.ps:114-166 then sums it with
    // hdiffuse. Here the two walks collapse into the loop below: the same lights,
    // the same volumes, the same plight_local, summed into the same total.
    vec3 dynamic = vec3(0.0);
    for (int i = 0; i < int(u_lightCount.x); ++i)
    {
        vec4 LdynPos = u_lights[i * 3 + 0];
        vec4 LdynColor = u_lights[i * 3 + 1];
        vec4 LdynDir = u_lights[i * 3 + 2];
        vec4 lparam = u_lightParams[i];

        if (lparam.x > 0.5)
        {
            // SPOT. The reference draws the solid of du_cone
            // (r4_rendertarget_accum_spot_geom.cpp:7-27): apex at the origin, a
            // base circle of radius 0.5 at z = 1 and its centre at z = 1 + EPS_L,
            // i.e. 18 vertices and 32 triangles, scaled by light::xform_calc
            // (light.cpp:278-285) with s = 2*range*tan(cone/2) on x/y and range on
            // z. In the light's own frame that solid is exactly
            //     0 <= z <= range   and   |xy| <= z * tan(cone/2)
            // and nothing else - there is no cone falloff in the shader, the cone
            // edge is the edge of the drawn geometry (accum_base.ps has no cone
            // term at all). Of the two conditions, z <= range is already enforced
            // by the attenuation: att = saturate(1 - rsqr * light_range_rsq) is
            // zero from rsqr = (range*0.95)^2 outwards, i.e. for every z > range.
            // So the apex plane and the lateral cone are the whole of the volume
            // test, and the direction is transformed as a direction
            // (accum_spot.cpp:127-129, transform_dir + normalize) because the view
            // matrix is a rotation and the angle is what the test measures.
            vec3 L2P = P - LdynPos.xyz;
            float z = dot(L2P, LdynDir.xyz);
            if (z <= 0.0)
                continue;
            float lateralSq = dot(L2P, L2P) - z * z;
            float tanHalf = lparam.y;
            if (lateralSq > z * z * tanHalf * tanHalf)
                continue;
        }
        // POINT needs no test: du_sphere (r4_rendertarget_accum_point_geom.cpp)
        // scaled by range (light.cpp:270-276) is the ball of radius range, which
        // is exactly the region where att is non-zero, so the geometry and the
        // attenuation describe the same solid and the shader is free of it.

        dynamic += plight_local(P, N, alb_gloss, LdynPos, LdynColor, mtl);
    }

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
    // hmodel.h:33-35 first overwrite the material: `m_terrain = abs(m - 0.95) <=
    // 0.04; if (m_terrain) m = 0;` - the whole of hmodel's albedo / specular /
    // rough then runs at the terrain's 0 id, not 0.95. The test is only reachable
    // now that m is the per-pixel G-buffer value, so it is ported here.
    float mAmbient = (abs(mtl - 0.95) <= 0.04) ? 0.0 : mtl;
    vec3 nw = mul(u_invView, vec4(N, 0.0)).xyz;
    vec3 cubeD = mix(sampleAmbientCube0(nw), sampleAmbientCube1(nw), u_ambient.w);
    // No cube on this level: fall back to the constant the port used before the
    // ambient cube was bound (the cube replaced by 1).
    cubeD = mix(vec3(1.0), cubeD, u_cubeValid.x);
    vec3 env_d = SRGBToLinearC(cubeD * u_hemiColor.rgb * hemi + u_ambient.rgb);

    // ----- ambient specular: hmodel.h:39-44, :64-78, :101-106, :121-134
    // The reflection vector, in the same space as the normal it is reflected
    // about. hmodel.h:52-53 is two lines, and both matter:
    //     Pnt     = normalize(Pnt);
    //     v2Pnt   = mul(m_inv_V, Pnt);
    // combine_1.ps:166 hands hmodel the view-space P, so hmodel.h:53 unprojects
    // it - and v2Pnt is WORLD space from there on: hmodel.h:64 reflects it
    // against nw (the unprojected normal of hmodel.h:48), and hmodel.h:145 hands
    // -v2Pnt to Amb_BRDF as the V of the N.V pair. Keeping P itself here - a
    // view-space vector - reflects the world normal in a view-space view vector
    // and then measures dot(N, V) across two different spaces, so the whole
    // ambient-specular arm was computed from a reflection direction the surface
    // never has.
    vec3 v2Pnt = mul(u_invView, vec4(normalize(P), 0.0)).xyz;
    // hmodel.h:64 - reflect(view vector, world normal), then the identical
    // remap + renormalise pair the diffuse taps use (hmodel.h:66-75).
    vec3 vreflect = reflect(v2Pnt, nw);
    vec3 vreflectRemap = normalize(cube_remap(vreflect));

    // hmodel.h:35, :39 - the roughness, and hmodel.h:44 the mip it filters the
    // cube with. calc_rough reads the raw gloss of alb_gloss, not the
    // linearised spec the light model makes. hmodel.h:35 runs it inside the
    // combine, where Ldynamic_color is the sun's (phase_combine.cpp:306), so
    // the exponent is the sun's own u_diffuse2s.
    float roughCube = calc_rough(alb_gloss, mAmbient, u_sunSpec.x);
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

    // hmodel.h:28 / :145 - m_flora, the `env_s * !m_flora` of the Amb_BRDF call:
    //     bool m_flora = abs(m - 0.15) <= 0.04;
    //     Amb_BRDF(roughCube, 0, specular, env_d, env_s * !m_flora, -v2Pnt, nw)
    // The test is against the per-pixel material id, so it is only reachable
    // now that the id comes out of the G-buffer: for the grass class (0.15,
    // deffer_grass.ps:101) it is true and the whole ambient specular is zeroed.
    bool m_flora = abs(mAmbient - MAT_FLORA) <= MAT_FLORA_ELIPSON;
    // hmodel.h:145 - the BRDF itself. albedo is the literal 0 of the reference,
    // so its diffuse arm drops out and the result is the specular arm alone.
    vec3 f0Ambient = calc_specular(alb_gloss, mAmbient);
    vec3 ambientSpecular = Amb_BRDF(roughCube, 0.0, f0Ambient, env_d,
                                    m_flora ? vec3(0.0) : env_s, -v2Pnt, nw);

    // hmodel.h:143, :146 - the ambient's own copy of the gloss channel (no
    // Ldynamic_color.w factor on this side) and the reference's spec*2 gain.
    float specAmbient = SRGBToLinearF(DEF_GLOSS);
    // hmodel.h:145-147, assembled in the reference's own order:
    //     hdiffuse = float4(Amb_BRDF(roughCube, 0, specular, env_d, env_s*!m_flora,
    //                                 -v2Pnt, nw), 0);
    //     hdiffuse *= spec*2.0f;
    //     hdiffuse += float4(env_d*(albedo*1.0f), 0);
    // so the BRDF half is scaled by spec*2 and the env_d*albedo half is added on
    // top unscaled - the sum of the two is the reference's hdiffuse.
    vec3 hdiffuse = ambientSpecular * (specAmbient * 2.0) + env_d * albedo;

    // combine_1.ps:183 - `hdiffuse *= occ`. It is the WHOLE hdiffuse the factor
    // multiplies, not only the env_d*albedo half of it: by the time this line
    // runs, hdiffuse holds both products of hmodel.h:146-147. The port applied
    // occ to env_d*albedo alone, on the reading that hspecular is the only
    // unoccluded term - but hspecular is a *different* output of hmodel (its
    // second out parameter, hmodel.h:22, which hmodel.h:150 sets to 0 and never
    // writes into hdiffuse). The commented-out `//hspecular *= occ` of
    // combine_1.ps:185 is about that second output, not about the BRDF term
    // inside hdiffuse, so the ambient specular in this sum is occluded too.
    hdiffuse *= occ;

    // combine_1.ps:187-188 sums the two halves into the linear accumulator and
    // then gamma-corrects it: color = LinearTosRGB(L.rgb + hdiffuse.rgb). L is the
    // accumulator, which by then holds the sun lobe of accum_sun.ps and every
    // point / spot volume the accumulator passes added (r4_R_lights.cpp:191-271),
    // so the dynamic term joins the sum in exactly that place.
    // The bgfx post chain expects the same encoding at this point - combine_ps.sc
    // hands the value to tonemap(), whose first act is SRGBToLinear (see the
    // comment on tonemap() there) - so the resolve has to close the same round
    // trip the reference does.
    gl_FragColor = vec4(LinearTosRGB(sun + dynamic + hdiffuse), 1.0);
}
