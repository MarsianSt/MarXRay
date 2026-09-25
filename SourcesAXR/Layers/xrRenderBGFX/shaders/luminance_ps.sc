$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);
SAMPLER2D(s_prev, 1);
uniform vec4 u_middleGray;
uniform vec4 u_luminanceParams;

float hdrLuminance(vec2 _tc)
{
    vec3 source = texture2D(s_image, _tc).rgb;
    return dot(source, vec3(0.3, 0.38, 0.22)) * 9.0;
}

float filteredLuminance(vec2 _tc)
{
    return texture2D(s_image, _tc).r;
}

float firstPass(vec2 _uv, vec2 _texel)
{
    vec2 offset = _texel * 0.25;
    float a = hdrLuminance(_uv + vec2(-offset.x, -offset.y));
    float b = hdrLuminance(_uv + vec2( offset.x, -offset.y));
    float c = hdrLuminance(_uv + vec2(-offset.x,  offset.y));
    float d = hdrLuminance(_uv + vec2( offset.x,  offset.y));
    return (a + b + c + d) * 0.25;
}

float filterPass(vec2 _uv, vec2 _texel)
{
    float sum = 0.0;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            vec2 offset = vec2((float(x) - 1.5) * _texel.x, (float(y) - 1.5) * _texel.y);
            sum += filteredLuminance(_uv + offset);
        }
    }
    return sum * 0.0625;
}

void main()
{
    float pass = u_luminanceParams.x;
    vec2 texel = vec2(1.0 / max(u_luminanceParams.y, 1.0), 1.0 / max(u_luminanceParams.z, 1.0));
    float result = 0.0;
    if (pass < 0.5)
        result = firstPass(v_texcoord0, texel);
    else if (pass < 1.5)
        result = filterPass(v_texcoord0, texel);
    else
    {
        result = filterPass(v_texcoord0, texel);
        float scale = u_middleGray.x / (result * u_middleGray.y + u_middleGray.z);
        float previous = texture2D(s_prev, vec2(0.5, 0.5)).x;
        float value = mix(previous, scale, u_middleGray.w);
        gl_FragColor = vec4(clamp(value, 1.0 / 128.0, 20.0), 0.0, 0.0, 1.0);
        return;
    }
    gl_FragColor = vec4(result, 0.0, 0.0, 1.0);
}
