$input v_texcoord0, v_viewPos
#include <bgfx_shader.sh>

uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    // Forward fog removed: single fog layer in combine (Anomaly).
    gl_FragData[0] = c;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
}
