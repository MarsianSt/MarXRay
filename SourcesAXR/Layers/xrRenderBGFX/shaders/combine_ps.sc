$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdr, 0);
uniform vec4 u_exposure;

void main()
{
    vec3 c = texture2D(s_hdr, v_texcoord0).rgb;
    vec3 x = c * u_exposure.x;
    x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    gl_FragColor = vec4(clamp(x, 0.0, 1.0), 1.0);
}
