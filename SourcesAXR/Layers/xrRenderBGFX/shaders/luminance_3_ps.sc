$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);
SAMPLER2D(s_prev, 1);
uniform vec4 u_middleGray;
uniform vec4 u_texel;

float sampleBilinear(vec2 _tc)
{
    vec2 h = 0.5 * u_texel.xy;
    vec4 a = texture2D(s_image, _tc + vec2(-h.x, -h.y));
    vec4 b = texture2D(s_image, _tc + vec2( h.x, -h.y));
    vec4 c = texture2D(s_image, _tc + vec2(-h.x,  h.y));
    vec4 d = texture2D(s_image, _tc + vec2( h.x,  h.y));
    return dot(a + b + c + d, vec4(0.0625));
}

void main()
{
    vec2 uv = v_texcoord0;
    float result = 0.0;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            vec2 offset = vec2((float(x) - 1.5) * u_texel.x,
                (float(y) - 1.5) * u_texel.y);
            result += sampleBilinear(uv + offset);
        }
    }
    result *= 0.0625;
    float scale = u_middleGray.x / (result * u_middleGray.y + u_middleGray.z);
    float previous = texture2D(s_prev, vec2(0.5, 0.5)).x;
    float value = mix(previous, scale, u_middleGray.w);
    gl_FragColor = vec4(clamp(value, 1.0 / 128.0, 20.0), 0.0, 0.0, 1.0);
}
