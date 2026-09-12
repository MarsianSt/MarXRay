$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(u_texture, 0);

void main()
{
    vec4 texColor = texture2D(u_texture, v_texcoord0);
    gl_FragColor  = texColor * v_color0;
}