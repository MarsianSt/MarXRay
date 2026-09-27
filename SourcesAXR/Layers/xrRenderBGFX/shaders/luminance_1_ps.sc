$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);
uniform vec4 u_texel;

float luminance(vec2 _tc)
{
    vec3 source = texture2D(s_image, _tc).rgb;
    return dot(source, vec3(0.3, 0.38, 0.22)) * 9.0;
}

void main()
{
    vec2 o = u_texel.xy * 0.75;
    vec2 uv = v_texcoord0;
    float a = luminance(uv + vec2(-o.x, -o.y));
    float b = luminance(uv + vec2( o.x, -o.y));
    float c = luminance(uv + vec2(-o.x,  o.y));
    float d = luminance(uv + vec2( o.x,  o.y));
    gl_FragColor = vec4(a, b, c, d);
}
