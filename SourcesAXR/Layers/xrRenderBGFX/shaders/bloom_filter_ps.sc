$input v_texcoord0

#include <bgfx_shader.sh>

// 1:1 with game_unpacked/shaders/r3/bloom_filter.ps, non ENCHANTED_SHADERS_ENABLED branch
// (lines 36-63). Driven by CBlender_bloom_build elements 1 and 2
// (archive_sourse/Layers/xrRenderPC_R4/blender_bloom_build.cpp:22-35): element 1 is the X filter
// (rt_Bloom_1 -> rt_Bloom_2), element 2 the Y filter (rt_Bloom_2 -> rt_Bloom_1), both reading
// s_bloom = the previous bloom RT with smp_rtlinear and the constant array weight[2] set by
// RCache.set_ca("weight", 0/1, w0/w1).
//
// Separable gauss filter: 	2*7 + 1 + 7*2 = 29 samples
// Samples:			0-central, -1, -2,..., -7, 1, 2,... 7
//
// The v_filter uv pairs (r4_rendertarget_phase_bloom.cpp:172-179 for X, :252-259 for Y) are
//   a_0 = half (both axes)                      half = { .5f/256, .5f/256 }
//   a_k = a_{k-1} -/+ two*axis,  two = 2/256    ->  taps at half +/- (2k-1+0.5) texels
// so the interpolated texcoord is offset by a_0 and the taps by (2k-0.5)/256 along the pass axis.
// u_filterSetup = (1/256, axis.x, axis.y, 0) with axis = (1,0) for X and (0,1) for Y.
// u_weight0 = W[1..4], u_weight1 = W[5..7] + W[0] (the central tap), exactly as the CPU side
// CalcGauss_wave packs them (r4_rendertarget_phase_bloom.cpp:50-51).
SAMPLER2D(s_bloom, 0);
uniform vec4 u_weight0;
uniform vec4 u_weight1;
uniform vec4 u_filterSetup;

void main()
{
    float step = u_filterSetup.x;
    vec2 axis = u_filterSetup.yz;
    vec2 base = v_texcoord0 + 0.5 * step;

    // central
    vec4 accum = u_weight1.w * texture2D(s_bloom, base);

    // left (7)
    // right (7)
    vec2 offset = 1.5 * step * axis;
    accum += u_weight0.x * texture2D(s_bloom, base - offset);
    accum += u_weight0.x * texture2D(s_bloom, base + offset);

    offset = 3.5 * step * axis;
    accum += u_weight0.y * texture2D(s_bloom, base - offset);
    accum += u_weight0.y * texture2D(s_bloom, base + offset);

    offset = 5.5 * step * axis;
    accum += u_weight0.z * texture2D(s_bloom, base - offset);
    accum += u_weight0.z * texture2D(s_bloom, base + offset);

    offset = 7.5 * step * axis;
    accum += u_weight0.w * texture2D(s_bloom, base - offset);
    accum += u_weight0.w * texture2D(s_bloom, base + offset);

    offset = 9.5 * step * axis;
    accum += u_weight1.x * texture2D(s_bloom, base - offset);
    accum += u_weight1.x * texture2D(s_bloom, base + offset);

    offset = 11.5 * step * axis;
    accum += u_weight1.y * texture2D(s_bloom, base - offset);
    accum += u_weight1.y * texture2D(s_bloom, base + offset);

    offset = 13.5 * step * axis;
    accum += u_weight1.z * texture2D(s_bloom, base - offset);
    accum += u_weight1.z * texture2D(s_bloom, base + offset);

    // OK
    gl_FragColor = accum;
}
