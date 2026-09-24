$input v_color1, v_texcoord0, v_texcoord1, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_clouds0, 0);
SAMPLER2D(s_clouds1, 1);
SAMPLER2D(s_tonemap, 2);

uniform vec4 u_fogColor;

void main()
{
    vec4 s0 = texture2D(s_clouds0, v_texcoord0);
    vec4 s1 = texture2D(s_clouds1, v_texcoord1);
    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;
    vec4 c = vec4(v_color1.rgb * scale, v_color1.a);
    vec4 col = c * (s0 + s1);
    // Horizon fog: blend rgb to weather fog_color at grazing view angles, keep alpha.
    // Narrow band (power 32): only the horizon line.
    vec3 vd = v_worldPos - u_invView[3].xyz;
    float fog = pow(1.0 - saturate(vd.y / length(vd)), 32.0);
    col.rgb = mix(col.rgb, u_fogColor.rgb, fog);
    gl_FragColor = col;
}
