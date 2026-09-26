$input v_color1, v_texcoord0, v_texcoord1

#include <bgfx_shader.sh>

SAMPLER2D(s_clouds0, 0);
SAMPLER2D(s_clouds1, 1);
SAMPLER2D(s_tonemap, 2);

void main()
{
    vec4 s0 = texture2D(s_clouds0, v_texcoord0);
    vec4 s1 = texture2D(s_clouds1, v_texcoord1);
    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;
    vec4 c = vec4(v_color1.rgb * scale, v_color1.a);
    vec4 col = c * (s0 + s1);
    gl_FragColor = col;
}
