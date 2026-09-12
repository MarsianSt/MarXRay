$input v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_alphaCtrl;
uniform vec4 u_detailCtrl;

SAMPLER2D(u_texture, 0);
SAMPLER2D(u_detail,  1);

void main()
{
    vec4 base = texture2D(u_texture, v_texcoord0);
    vec4 det  = texture2D(u_detail,  v_texcoord0 * u_detailCtrl.x);
    vec4 o    = base * det;
    if (base.a < u_alphaCtrl.x)
        discard;
    gl_FragColor = vec4(o.rgb, 1.0);
}