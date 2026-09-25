$input v_texcoord0, v_fogDepth
#include <bgfx_shader.sh>

uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    // Forward fog removed: single SSFX screenspace layer in combine (Anomaly).
    gl_FragColor = c;
}
