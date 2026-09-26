$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdr, 0);
SAMPLER2D(s_tonemap, 1);
// Position G-buffer (attachment 1 of the scene FB): view-space position written by
// every world/particle/wallmark PS. Anomaly reads the very same value out of its gbuf
// position target (combine_1.ps:194-201), so the fog branch is a 1:1 port.
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
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;
uniform vec4 u_lowlandFogParams;
uniform vec4 u_sunDir;
uniform vec4 u_sunColor;

// u_view is the bgfx predefined per-view uniform (declared by <bgfx_shader.sh>, fed by
// bgfx_set_view_transform on kCombineView), i.e. the mirror of the AXR m_v2w / m_inv_V
// that compute_height_fog and combine_1.ps:194 use. It is used here undeclared.

// game_unpacked/shaders/r3/srgb.h:7-53, cheap gamma (pow 2.2 in, pow 1/2.2 out).
float SRGBToLinearF(float x) { return pow(max(0.0, x), 2.2); }
vec3 SRGBToLinearC(vec3 x) { return vec3(SRGBToLinearF(x.r), SRGBToLinearF(x.g), SRGBToLinearF(x.b)); }
float LinearTosRGBF(float x) { return pow(max(0.0, x), 0.45454545); }
vec3 LinearTosRGB(vec3 x) { return vec3(LinearTosRGBF(x.r), LinearTosRGBF(x.g), LinearTosRGBF(x.b)); }

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

// game_unpacked/shaders/r3/common_functions.h:49-75 blend_soft, ported 1:1 (HLSL inout/out
// params -> explicit returns). ACES_LMT(b) is left out: without USE_ACES it is Color_Grading
// (ASC-CDL with Slope 1 / Offset 0 / Power 1 / Saturation 1) followed by Contrast_Reduction,
// and the anomaly build drives that CDL from pp_img_corrections / pp_img_cg, which the bgfx
// port does not bind (ACES_Color_Grading.h:21-55, ACES_LMT.h:11-31,
// ACES_LMTs/LMT_Contrast_Reduction.h:7-11).
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

// game_unpacked/shaders/r3/tonemap_srgb.h:6-40 tonemap_sRGB (non-USE_ACES branch) fused with
// game_unpacked/shaders/r3/common_functions.h:23-33 tonemap().
//
// Reference chain: SRGBToLinear -> *scale -> LinearTosRGB -> SRGBToLinear -> ACES_LMT ->
// max(0) -> contrast boost -> Reinhard -> / (w/(w+1)) -> LinearTosRGB -> saturate, with
// w = fWhiteIntensity = 11.2. The LinearTosRGB/SRGBToLinear pair in the middle is a pure
// round trip (both are pow(x, 1/2.2) / pow(x, 2.2), and both clamp at 0), so it is
// elided here exactly as the AXR pair gamma-corrects the lighting at combine_1.ps:188 and
// undoes it at the head of tonemap(). Everything else is 1:1; the CDL in ACES_LMT is the
// identity (Slope 1 / Offset 0 / Power 1 / Saturation 1) because the anomaly build feeds it
// from pp_img_corrections / pp_img_cg, which the bgfx port does not bind.
vec3 tonemap(vec3 rgb, float scale)
{
    rgb = SRGBToLinearC(rgb);
    rgb = rgb * scale;

    //ACES_LMT -> Contrast_Reduction (ACES_LMTs/LMT_Contrast_Reduction.h:6-11)
    float Contrast_Amount = 0.7;
    const float mid = 0.18;
    rgb = pow(rgb, Contrast_Amount) * mid / pow(mid, Contrast_Amount);

    //clamp negative values (tonemap_srgb.h:18-19)
    rgb = max(vec3(0.0, 0.0, 0.0), rgb);

    //Boost the contrast to match ACES RRT (tonemap_srgb.h:21-23)
    float Contrast_Boost = 1.42857;
    rgb = pow(rgb, Contrast_Boost) * mid / pow(mid, Contrast_Boost);

    //reinhard tonemapping (tonemap_srgb.h:25-27)
    rgb = rgb / (rgb + 1.0);
    rgb /= 11.2 / 12.2;

    //convert into sRGB gamma space (tonemap_srgb.h:29-30)
    rgb = LinearTosRGB(rgb);

    //return with saturate, everything should be in LDR sRGB (tonemap_srgb.h:39)
    return saturate(rgb);
}

void main()
{
    vec2 tc = v_texcoord0;
    vec3 c = texture2D(s_hdr, tc).rgb;

    // Anomaly reads P.xyz from the G-buffer position target (combine_1.ps:194); the bgfx
    // port writes the same view-space position into attachment 1 of the scene FB, so no
    // depth reconstruction is needed and the depth attachment is only there for the
    // depth test. Sky and clouds never write it, so P stays 0 there and both fog terms
    // evaluate to 0, which is why the sky stays clear of fog.
    vec3 P = texture2D(s_position, tc).xyz;

    // game_unpacked/shaders/r3/combine_1.ps:196-202, the active branch (LOWLAND_FOG_TYPE != 1).
    float distance = length(P);
    float fog = saturate(distance * u_fogParams.w + u_fogParams.x);
    c = lerp(c, u_fogColor.rgb, fog);
    c = lerp(c, u_fogColor.rgb, compute_height_fog(P));

    // combine_1.ps:204 feeds o.low.a / o.high.a with this and the runtime uses the alpha
    // to blend the sky over the scene (DX9 render targets; the R4 combine resolves the sky
    // in the shader instead). Deliberate deviation: writing it into the backbuffer alpha here
    // would break the UI compositing that runs after this pass, so the value is only computed.
    float skyblend = saturate(fog * fog);

    // combine_1.ps:206 tm_scale, i.e. the luminance chain output sampled as before.
    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;

    // game_unpacked/shaders/r3/combine_1.ps:213 tonemap(o.low, o.high, color, tm_scale).
    vec3 low = tonemap(c, u_exposure.x * scale);

    // game_unpacked/shaders/r3/combine_2_aa.ps:123 - combine_bloom(final, s_bloom.Sample(...)).
    gl_FragColor = combine_bloom(low, texture2D(s_bloom, tc));
}
