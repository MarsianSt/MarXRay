$input v_texcoord0, v_fogDepth

#include <bgfx_shader.sh>

uniform vec4 u_alphaCtrl;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    if (u_alphaCtrl.y > 0.5 && c.a < u_alphaCtrl.x)
        discard;
    float fog = saturate(v_fogDepth * u_fogParams.w + u_fogParams.x);
    c.rgb = mix(c.rgb, u_fogColor.rgb, fog);
    gl_FragColor = c;
}
