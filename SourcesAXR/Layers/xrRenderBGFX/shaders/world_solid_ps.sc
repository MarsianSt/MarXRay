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
    // Forward fog removed: screenspace SSFX fog in combine is the single layer
    // (as in Anomaly); keeping both gives 2f-f^2 overfog.
    gl_FragColor = c;
}
