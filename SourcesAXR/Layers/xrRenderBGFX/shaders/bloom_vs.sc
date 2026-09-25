$input a_position, a_texcoord0
$output v_texcoord0

#include <bgfx_shader.sh>

// The R4 bloom blenders use g_bloom_build / g_bloom_filter, which emit screen space
// positions plus up to 8 precomputed uv pairs (r4_rendertarget_phase_bloom.cpp:103-116 /
// :182-231). The tap uvs are a_i .. 1+a_i, i.e. the interpolated texcoord is the pixel's
// normalized position inside the 256x256 target shifted by the half texel a_0, which
// bloom_build_ps.sc / bloom_filter_ps.sc add themselves. So one plain uv varying is enough.
void main()
{
    gl_Position = vec4(a_position, 1.0);
    v_texcoord0 = a_texcoord0;
}
