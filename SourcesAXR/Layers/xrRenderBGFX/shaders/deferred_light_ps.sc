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
// Not ported here, deliberately: the specular half of compute_lighting
// (lmodel.h:148-173) and hmodel's Amb_BRDF term (hmodel.h:145), the SSAO factor
// (combine_1.ps:128-159) and the sun shadow term (accum_sun.ps:26-32). They need
// the material LUT (s_material) and the shadow map, none of which this port
// binds.

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

// u_invView is the bgfx predefined per-view uniform (bgfx_shader.sh:836), the
// mirror of the AXR m_inv_V that hmodel.h:48 multiplies the normal with; the
// resolve view is fed Device.mView (bgfxHDR.cpp, ResolvePass) for exactly that.

// game_unpacked/shaders/r3/srgb.h:7-53, same pair combine_ps.sc uses.
float SRGBToLinearF(float x) { return pow(max(0.0, x), 2.2); }
vec3 SRGBToLinearC(vec3 x) { return vec3(SRGBToLinearF(x.r), SRGBToLinearF(x.g), SRGBToLinearF(x.b)); }
float LinearTosRGBF(float x) { return pow(max(0.0, x), 0.45454545); }
vec3 LinearTosRGB(vec3 x) { return vec3(LinearTosRGBF(x.r), LinearTosRGBF(x.g), LinearTosRGBF(x.b)); }

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

    // lmodel.h:118-120 - compute_lighting linearises the G-buffer albedo before
    // it modulates anything, and combine_1.ps:161 gamma-corrects D.rgb the same
    // way for the ambient stored in the accumulator alpha.
    vec3 albedo = SRGBToLinearC(D.rgb);

    // ----- sun: accum_sun.ps:23-35
    // plight_infinity (lmodel.h:180-183): L = -normalize(light_direction) with
    // light_direction = Ldynamic_dir, which the port's u_sunDir already is in
    // view space; the eye vector is only needed by the specular half.
    vec3 L = -normalize(u_sunDir.xyz);
    float NdotL = saturate(dot(N, L));

    // lmodel.h:142, :146-147 - the non-LUT diffuse of the non-ES_PSEUDO_PBR
    // branch, with the s_material lookup of lmodel.h:133 skipped (not bound):
    //   metalness = ceil(mat_id - 0.75)
    //   gloss     = saturate(mat_id/0.75) - 0.5*metalness
    //   exponent  = ((gloss*0.5)*0.8) + 0.6
    //   light.rgb = pow(NdotL, exponent) * ((exponent + 1.0) * 0.5)
    float metalness = ceil(GBUF_MTL - 0.75);
    float gloss = saturate(GBUF_MTL / 0.75);
    gloss -= 0.5 * metalness;
    float exponent = ((gloss * 0.5) * 0.8) + 0.6;
    vec3 sun = pow(NdotL, exponent) * ((exponent + 1.0) * 0.5) * albedo;

    // accum_sun.ps:34-35: SRGBToLinear(s) is the shadow term and is 1 here (no
    // shadow map in this pass), then the same gamma correction on the sun colour
    // the reference applies to Ldynamic_color.rgb.
    sun *= SRGBToLinearC(u_sunColor.rgb);

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
    vec3 ambient = env_d * albedo;

    // combine_1.ps:187-188 sums the two halves into the linear accumulator and
    // then gamma-corrects it: color = LinearTosRGB(L.rgb + hdiffuse.rgb). The
    // bgfx post chain expects the same encoding at this point - combine_ps.sc
    // hands the value to tonemap(), whose first act is SRGBToLinear (see the
    // comment on tonemap() there) - so the resolve has to close the same round
    // trip the reference does.
    gl_FragColor = vec4(LinearTosRGB(sun + ambient), 1.0);
}
