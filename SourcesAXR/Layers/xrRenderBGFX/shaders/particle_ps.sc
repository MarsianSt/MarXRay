$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_base, 0);
uniform vec4 u_particleParams;

void main()
{
    vec4 texColor = texture2D(s_base, v_texcoord0);
    vec4 c = texColor * v_color0;
    if (u_particleParams.y > 0.5 && c.a * 255.0 < u_particleParams.x)
        discard;
    gl_FragColor = c;
}
