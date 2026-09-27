$input v_texcoord0

#include <bgfx_shader.sh>

// 1:1 with game_unpacked/shaders/r3/combine_2_naa.ps:106-121 (G_FOG_USE_SCATTERING,
// which is ON in settings_screenspace_FOG.h). Blurred scene bleeds over the
// fogged transition zone, which is what makes the AXR horizon seamless.
// fogresult here is the distance term only (SSFX_CALC_FOG, no height), squared,
// exactly like the reference. disablefog (scopes/NVG) is not wired: always 1.
// s_position / u_fogParams share names (and bgfx handles) with combine_ps.sc.
SAMPLER2D(s_scatterImage, 0);
SAMPLER2D(s_scatterBlur, 1);
SAMPLER2D(s_position, 2);

uniform vec4 u_fogParams;

void main()
{
    vec3 img = texture2D(s_scatterImage, v_texcoord0).rgb;
    vec3 P = texture2D(s_position, v_texcoord0).xyz;
    float fogresult = saturate(length(P) * u_fogParams.w + u_fogParams.x);
    fogresult *= fogresult;
    vec3 foggg = texture2D(s_scatterBlur, v_texcoord0).rgb;
    img = mix(img, max(img, foggg), smoothstep(0.2, 0.8, fogresult));
    gl_FragColor = vec4(img, 1.0);
}
