$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);
SAMPLER2D(s_prev, 1);
uniform vec4 u_middleGray;
uniform vec4 u_luminanceParams;

// common_defines.h:11 - def_hdr float(9.h). A compile-time constant in the reference
// too, so the x9 stays a shader constant and not a tunable.
const float DEF_HDR = 9.0;

float hdrLuminance(vec2 _tc)
{
    vec3 source = texture2D(s_image, _tc).rgb;
    // bloom_luminance_1.ps:6-10, 1:1:
    //   return dot(s_image.Sample(smp_rtlinear, tc), LUMINANCE_VECTOR*def_hdr);
    // LUMINANCE_VECTOR is anomaly_shaders.h:9, float3(0.2125, 0.7154, 0.0721).
    // s_image is r2_RT_bloom1 (blender_luminance.cpp:18), i.e. the bright-pass output
    // bloom_build.ps:46 writes in the high domain, so the x9 is the high -> pre-tonemap
    // undo and the result lands in the sRGB domain: result = luma(sRGB_encode(linear*scale)).
    return dot(source, vec3(0.2125, 0.7154, 0.0721) * DEF_HDR);
}

float filteredLuminance(vec2 _tc)
{
    return texture2D(s_image, _tc).r;
}

// Pass 0 (bloom_luminance_1.ps:18-44): the four reference taps of v_build over the
// 256x256 rt_Bloom_1. The port approximates the reference corner layout (a_0..a_3
// spanning one and two texels, r4_rendertarget_phase_luminance.cpp:39-44, plus the
// bilinear filter, i.e. a 4x4 texel area) with a +/-0.25 texel box; that layout predates
// the split-HDR work and is left as is, only the source and the domain changed.
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
