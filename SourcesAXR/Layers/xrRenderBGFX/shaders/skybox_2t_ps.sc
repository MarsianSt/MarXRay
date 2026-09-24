$input v_color0, v_dir

#include <bgfx_shader.sh>

SAMPLERCUBE(s_sky0, 0);
SAMPLERCUBE(s_sky1, 1);
SAMPLER2D(s_tonemap, 2);

uniform vec4 u_fogColor;

void main()
{
    vec3 dir = normalize(v_dir);
    vec3 s0 = textureCube(s_sky0, dir);
    vec3 s1 = textureCube(s_sky1, dir);
    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;
    vec3 tint = v_color0.rgb * (scale * 2.0);
    vec3 sky = tint * mix(s0, s1, v_color0.a);
    sky *= 0.33;
    // Horizon fog: blend to weather fog_color at grazing angles.
    float fog = pow(1.0 - saturate(dir.y), 8.0);
    sky = mix(sky, u_fogColor.rgb, fog);
    gl_FragColor = vec4(sky, 1.0);
}
