$input v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_alphaCtrl;

SAMPLER2D(u_texture, 0);

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    if (u_alphaCtrl.y > 0.5 && c.a < u_alphaCtrl.x)
        discard;
    gl_FragColor = c;
}