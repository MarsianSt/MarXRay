$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdr, 0);
SAMPLER2D(s_tonemap, 1);
SAMPLER2D(s_position, 2);
// r2_RT_bloom1, the target the R4 vertical gaussian leaves behind
// (archive_sourse/Layers/xrRenderPC_R4/blender_bloom_build.cpp:22-35 element 2,
// r4_rendertarget_phase_bloom.cpp:317-322). Bound from bgfxHDR::GetBloomTexture.
SAMPLER2D(s_bloom, 3);
uniform vec4 u_exposure;

// Anomaly r3 fog globals, fed from CEnvironment::CurrentEnv by bgfxHDR::SetFogUniforms.
//   fog_params         game_unpacked/shaders/r3 -> Blender_Recorder_StandartBinding.cpp:177
//   fog_color          -> Blender_Recorder_StandartBinding.cpp:190
//   lowland_fog_params -> Blender_Recorder_StandartBinding.cpp:206
//   Ldynamic_dir/color -> archive_sourse/Layers/xrRenderPC_R4/r4_rendertarget_phase_combine.cpp:252-253
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;
uniform vec4 u_lowlandFogParams;
uniform vec4 u_sunDir;
uniform vec4 u_sunColor;

// u_view is the bgfx predefined per-view uniform (declared by <bgfx_shader.sh>, fed by
// bgfx_set_view_transform on kCombineView), i.e. the mirror of the Anomaly m_inv_V that
// combine_1.ps:194 uses to turn the view-space position into a world-space one.


// game_unpacked/shaders/r3/common_functions.h:387-407 compute_height_fog, ported 1:1
// (HLSL float3 param -> explicit return, m_v2w -> u_view).
float compute_height_fog(vec3 P_view)
{
    //Settings
    float height = u_lowlandFogParams.x;    //Fog height
    float density = u_lowlandFogParams.y;    //Fog density (keep it low, it's exponential fog without any distance attenuation)
    float base_height = u_lowlandFogParams.z; //Fog base height (base height of lowland fog, for current level)

    //Transform view space position into world space
    vec3 P_world = mul(u_view, vec4(P_view, 1.0)).xyz;

    //Calculate height factor
    float height_factor = base_height + height - P_world.y;

    //Get length of view space position
    float P_dist = length(P_view.xyz) * height_factor;

    //Calculate exponential fog
    float fog = 1.0 - exp(-P_dist * density);

    //Output
    return saturate(fog);
}

// game_unpacked/shaders/r3/srgb.h:7-53, cheap gamma (pow 2.2 in, pow 1/2.2 out).
float SRGBToLinearF(float x) { return pow(max(0.0, x), 2.2); }
vec3 SRGBToLinearC(vec3 x) { return vec3(SRGBToLinearF(x.r), SRGBToLinearF(x.g), SRGBToLinearF(x.b)); }
float LinearTosRGBF(float x) { return pow(max(0.0, x), 0.45454545); }
vec3 LinearTosRGB(vec3 x) { return vec3(LinearTosRGBF(x.r), LinearTosRGBF(x.g), LinearTosRGBF(x.b)); }

// game_unpacked/shaders/r3/common_functions.h:49-75 blend_soft, ported 1:1 (HLSL inout/out
// params -> explicit returns). ACES_LMT(b) is left out: without USE_ACES it is Color_Grading
// (ASC-CDL with Slope 1 / Offset 0 / Power 1 / Saturation 1) followed by Contrast_Reduction,
// and the anomaly build drives that CDL from pp_img_corrections / pp_img_cg, which the bgfx
// port does not bind (ACES_Color_Grading.h:21-55, ACES_LMT.h:11-31,
// ACES_LMTs/LMT_Contrast_Reduction.h:7-11). The reference contrast reduction (0.7) and boost
// (1.42857 = 1/0.7) around mid 0.18 multiply out to exactly 1, and so do the
// SRGBToLinear / inverse-tonemap / LinearTosRGB round trip, so with an empty bloom buffer this
// returns `low` unchanged.
vec3 blend_soft(vec3 low, vec3 high)
{
    //gamma correct and inverse tonemap to add bloom
    vec3 a = SRGBToLinearC(low); //post tonemap render
    a = a / max(0.004, 1.0 - a); //inverse tonemap
    vec3 b = SRGBToLinearC(high); //bloom

    //constrast reduction of ACES output
    float Contrast_Amount = 0.7;
    const float mid = 0.18;
    a = pow(a, Contrast_Amount) * mid / pow(mid, Contrast_Amount);

    a += b; //bloom add

    //Boost the contrast to match ACES RRT
    float Contrast_Boost = 1.42857;
    a = pow(a, Contrast_Boost) * mid / pow(mid, Contrast_Boost);

    a = a / (1.0 + a); //tonemap

    return LinearTosRGB(a);
}

// game_unpacked/shaders/r3/common_functions.h:77-82 combine_bloom, ported 1:1
vec4 combine_bloom(vec3 low, vec4 high)
{
    high.rgb *= high.a;
    return vec4(blend_soft(low, high.rgb), 1.0); //screen
}

void main()
{
    vec2 tc = v_texcoord0;
    vec3 c = texture2D(s_hdr, tc).rgb;

    // Position G-buffer (attachment 1 of the scene FB): the view-space position written
    // by every world/particle/wallmark PS, the direct equivalent of the Anomaly gbuf
    // position that combine_1.ps:194 reads. Sky and clouds do not write it, so P stays
    // 0 there and the depth attachment is only needed for the depth test.
    vec3 P = texture2D(s_position, tc).xyz;
    vec3 WorldP = mul(u_view, vec4(P, 1.0)).xyz;

    // game_unpacked/shaders/r3/combine_1.ps:196-202, the active branch
    // (LOWLAND_FOG_TYPE != 1): vanilla distance fog, then the exponential lowland fog.
    float distance = length(P);
    float fog = saturate(distance * u_fogParams.w + u_fogParams.x);
    c = lerp(c, u_fogColor.rgb, fog);
    c = lerp(c, u_fogColor.rgb, compute_height_fog(P));

    // combine_1.ps:204 - computed, but deliberately not written into the backbuffer
    // alpha: that would break the UI compositing which runs after this pass.
    float skyblend = saturate(fog * fog);

    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;
    vec3 x = c * u_exposure.x * scale;
    vec3 low = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);

    // game_unpacked/shaders/r3/combine_2_aa.ps:123 - combine_bloom(final, s_bloom.Sample(...)).
    gl_FragColor = combine_bloom(low, texture2D(s_bloom, tc));
}
