$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdr, 0);
SAMPLER2D(s_tonemap, 1);
SAMPLER2D(s_hdrDepth, 2);
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

void main()
{
    vec2 tc = v_texcoord0;
    vec3 c = texture2D(s_hdr, tc).rgb;

    // Anomaly reads P.xyz from the G-buffer position target; the screenspace
    // equivalent is the view-space position rebuilt from the HDR depth buffer.
    // combine_1.ps:194 does mul(m_inv_V, float4(P.xyz, 1)) for WorldP.y.
    float depth = texture2D(s_hdrDepth, tc).x;
    vec4 ndc = vec4(tc.x * 2.0 - 1.0, 1.0 - tc.y * 2.0, depth, 1.0);
    vec4 viewPos = mul(u_invProj, ndc);
    vec3 P = viewPos.xyz / viewPos.w;
    vec3 WorldP = mul(u_invView, vec4(P, 1.0)).xyz;

    c = SSFX_HEIGHT_FOG(P, WorldP.y, c);

    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;
    vec3 x = c * u_exposure.x * scale;
    x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    gl_FragColor = vec4(clamp(x, 0.0, 1.0), 1.0);
}
