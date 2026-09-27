$input v_texcoord0

#include <bgfx_shader.sh>

// The high half of the Anomaly split-HDR pair, 1:1 with the high line of tonemap()
// (game_unpacked/shaders/r3/common_functions.h:23-33, the function combine_1.ps:213
// calls as tonemap(o.low, o.high, color, tm_scale)):
//
//   rgb  = SRGBToLinear(rgb);            // :25
//   rgb  = rgb * scale;                  // :26  (tm_scale, combine_1.ps:206)
//   rgb  = LinearTosRGB(rgb);            // :27
//   low  = tonemap_sRGB(rgb, 11.2);      // :31  <- combine_ps.sc tonemap()
//   high = rgb / def_hdr;                // :32  <- THIS PASS
//
// The sky branch of the same encoding is sky2.ps:59-60, o.high = o.low/def_hdr, and
// the volumetric one is combine_volumetric.ps:33. Nothing else in r3 writes a second
// SV_Target, and nothing but the bloom bright pass (bloom_build.ps:27-30, fed
// r2_RT_generic1 by blender_bloom_build.cpp:18) ever reads it back.
//
// Inputs therefore mirror what combine_1.ps hands to tonemap(): s_hdr is the lit,
// gamma-corrected accumulator (combine_1.ps:187-188 color = LinearTosRGB(L + hdiffuse),
// which the bgfx lighting resolve already wrote - deferred_light_ps.sc:202), s_position
// is the G-buffer position the fog block reads (combine_1.ps:198), s_tonemap is
// r2_RT_luminance_cur (combine_1.ps:206) and the fog uniforms are the r3 globals.
//
// alpha is skyblend, 1:1 with combine_1.ps:214-215 (o.low.a = o.high.a = skyblend);
// the R4 read path only samples .rgb, so nothing downstream depends on it.

// common_defines.h:11 - def_hdr float(9.h). A compile-time constant there too, so the
// /9 stays a shader constant instead of becoming a tunable.
const float DEF_HDR = 9.0;

SAMPLER2D(s_hdr, 0);
SAMPLER2D(s_tonemap, 1);
SAMPLER2D(s_position, 2);
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;
uniform vec4 u_lowlandFogParams;

// u_view is the bgfx predefined per-view uniform (bgfx_shader.sh), the mirror of the AXR
// m_v2w that compute_height_fog uses; it is fed by bgfx_set_view_transform on kHighView.

// game_unpacked/shaders/r3/srgb.h:7-53, cheap gamma (pow 2.2 in, pow 1/2.2 out).
float SRGBToLinearF(float x) { return pow(max(0.0, x), 2.2); }
vec3 SRGBToLinearC(vec3 x) { return vec3(SRGBToLinearF(x.r), SRGBToLinearF(x.g), SRGBToLinearF(x.b)); }
float LinearTosRGBF(float x) { return pow(max(0.0, x), 0.45454545); }
vec3 LinearTosRGB(vec3 x) { return vec3(LinearTosRGBF(x.r), LinearTosRGBF(x.g), LinearTosRGBF(x.b)); }

// common_functions.h:387-407 compute_height_fog, 1:1 with the copy in combine_ps.sc.
float compute_height_fog(vec3 P_view)
{
    //Settings
    float height = u_lowlandFogParams.x;    //Fog height
    float density = u_lowlandFogParams.y;    //Fog density
    float base_height = u_lowlandFogParams.z; //Fog base height for current level

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

void main()
{
    vec2 tc = v_texcoord0;
    vec3 c = texture2D(s_hdr, tc).rgb;
    vec3 P = texture2D(s_position, tc).xyz;

    // combine_1.ps:198-202, the active branch (LOWLAND_FOG_TYPE != 1). The fog is
    // applied before tonemap(), so it is part of the high channel as well - this is
    // the same block combine_ps.sc runs, kept 1:1 so the two stay in lockstep.
    float distance = length(P);
    float fog = saturate(distance * u_fogParams.w + u_fogParams.x);
    c = lerp(c, u_fogColor.rgb, fog);
    c = lerp(c, u_fogColor.rgb, compute_height_fog(P));

    // combine_1.ps:204
    float skyblend = saturate(fog * fog);

    // combine_1.ps:206 - r2_RT_luminance_cur. The reference binds it before
    // phase_luminance of this frame runs, i.e. the result of the previous one; the
    // bgfx post chain keeps the same index for the whole frame (bgfxHDR.cpp,
    // EndFrameLuminance) so this pass, the sky and the combine all read one value.
    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;

    // common_functions.h:25-27, i.e. exactly the head of combine_ps.sc tonemap() with
    // tm_scale already folded in, and :32 for the division.
    vec3 rgb = LinearTosRGB(SRGBToLinearC(c) * scale);

    gl_FragColor = vec4(rgb / DEF_HDR, skyblend);
}
