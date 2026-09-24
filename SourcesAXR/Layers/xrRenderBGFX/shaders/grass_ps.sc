$input v_color0, v_texcoord0
#include <bgfx_shader.sh>

uniform vec4 u_grassAlpha;

SAMPLER2D(u_texture, 0);

void main()
{
    vec4 base = texture2D(u_texture, v_texcoord0);
    if (u_grassAlpha.y > 0.5 && base.w < u_grassAlpha.x)
        discard;
    gl_FragColor = vec4(base.rgb * v_color0.rgb, base.w * v_color0.a);
}
