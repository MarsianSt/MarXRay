$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdr, 0);
SAMPLER2D(s_tonemap, 1);
SAMPLER2D(s_hdrDepth, 2);
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

// u_invProj / u_invView are the bgfx predefined per-view uniforms (declared by
// <bgfx_shader.sh>, fed by bgfx_set_view_transform on kCombineView), so they are
// used here without re-declaring. They mirror the Anomaly m_inv_P / m_inv_V that
// combine_1.ps:194 uses to rebuild WorldP from the view-space position.

// settings_screenspace_FOG.h, hardcoded exactly as in the reference.
// G_FOG_HEIGHT / G_FOG_HEIGHT_INTENSITY are dead code under
// G_USE_PARAMS_FROM_WEATHER (the branch we port, weather-driven values).
//   #define G_FOG_HEIGHT            8.0f
//   #define G_FOG_HEIGHT_INTENSITY  1.0f
#define G_FOG_HEIGHT_DENSITY       1.3
#define G_FOG_SUNCOLOR_INTENSITY   0.1

// game_unpacked/shaders/r3/screenspace_fog.h:11 SSFX_HEIGHT_FOG, ported 1:1
// (G_USE_PARAMS_FROM_WEATHER active). HLSL inout param -> explicit return.
vec3 SSFX_HEIGHT_FOG(vec3 P, float World_Py, vec3 color)
{
    // Get Sun dir
    vec3 Sun = saturate(dot(normalize(u_sunDir.xyz), -normalize(P)));

    // Apply sun color
    Sun = lerp(u_fogColor.rgb, u_sunColor.rgb, Sun);

    // Distance Fog ( Default Anomaly Fog )
    float fog = saturate(length(P) * u_fogParams.w + u_fogParams.x);

    // Height Fog
    float fogheight = smoothstep(u_lowlandFogParams.x, -u_lowlandFogParams.x, World_Py) * u_lowlandFogParams.y;

    // Add the height fog to the distance fog
    float fogresult = saturate(fog + fogheight * (fog * G_FOG_HEIGHT_DENSITY));

    // Blend factor to mix sun color and fog color. Adjust intensity to.
    float FogBlend = fogheight * G_FOG_SUNCOLOR_INTENSITY;

    // Final fog color
    vec3 FOG_COLOR = lerp(u_fogColor.rgb, Sun, FogBlend);

    // Apply fog to color
    return lerp(color, FOG_COLOR, fogresult);
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

    // Anomaly reads P.xyz from the G-buffer position target; the screenspace
    // equivalent is the view-space position rebuilt from the HDR depth buffer.
    // combine_1.ps:194 does mul(m_inv_V, float4(P.xyz, 1)) for WorldP.y.
    float depth = texture2D(s_hdrDepth, tc).x;
    // Sky (far plane, depth ~1.0) stays clear so clouds are always visible;
    // fog applies only to geometry. Threshold 0.9999 keeps distant objects fogged.
    if (depth < 0.9999)
    {
        vec4 ndc = vec4(tc.x * 2.0 - 1.0, 1.0 - tc.y * 2.0, depth, 1.0);
        vec4 viewPos = mul(u_invProj, ndc);
        vec3 P = viewPos.xyz / viewPos.w;
        vec3 WorldP = mul(u_invView, vec4(P, 1.0)).xyz;

        c = SSFX_HEIGHT_FOG(P, WorldP.y, c);
    }

    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;
    vec3 x = c * u_exposure.x * scale;
    vec3 low = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);

    // game_unpacked/shaders/r3/combine_2_aa.ps:123 - combine_bloom(final, s_bloom.Sample(...)).
    gl_FragColor = combine_bloom(low, texture2D(s_bloom, tc));
}
