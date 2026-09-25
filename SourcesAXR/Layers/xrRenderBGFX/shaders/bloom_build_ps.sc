$input v_texcoord0

#include <bgfx_shader.sh>

// 1:1 with game_unpacked/shaders/r3/bloom_build.ps, non ENCHANTED_SHADERS_ENABLED branch
// (lines 41-46). Driven by CBlender_bloom_build element 0,
// archive_sourse/Layers/xrRenderPC_R4/blender_bloom_build.cpp:15-20: s_image = r2_RT_generic1
// (the HDR scene target), sampled with smp_rtlinear. b_params = (s,s,s,f_bloom_factor) with
// s = ps_r2_ls_bloom_threshold (r4_rendertarget_phase_bloom.cpp:119-125); only b_params.x is
// read by the ported branch.
//
// The 4 taps of v_build (r4_rendertarget_phase_bloom.cpp:93-100) are a_0/a_1/a_2/a_3:
//   a_0 = half                       half = { .5f/width, .5f/height }
//   a_1 = half + (one.x, 0)          one  = { 1/width, 1/height } * { w/2/256, h/2/256 }
//   a_2 = half + (0, one.y)                             = { .5f/256, .5f/256 }
//   a_3 = half + one
// u_bloomSetup = (half.xy, one.xy). The RT is not cleared (phase_bloom:77) and the element
// blends SRCALPHA/INVSRCALPHA (blender_bloom_build.cpp:16), so this pass accumulates onto the
// previous rt_Bloom_1; the vertical gaussian then overwrites it.
SAMPLER2D(s_image, 0);
uniform vec4 u_bloomParams;
uniform vec4 u_bloomSetup;

void main()
{
    vec2 base = v_texcoord0 + u_bloomSetup.xy;
    vec2 one = u_bloomSetup.zw;

    // hi-rgb.base-lum
    vec3 s0 = texture2D(s_image, base).rgb;
    vec3 s1 = texture2D(s_image, base + vec2(one.x, 0.0)).rgb;
    vec3 s2 = texture2D(s_image, base + vec2(0.0, one.y)).rgb;
    vec3 s3 = texture2D(s_image, base + one).rgb;

    vec3 avg = ((s0 + s1) + (s2 + s3)) / 2;
    float hi = dot(avg, vec3(1.0)) - u_bloomParams.x;    // assume def_hdr equal to 3.0

    gl_FragColor = vec4(avg, hi);
}
