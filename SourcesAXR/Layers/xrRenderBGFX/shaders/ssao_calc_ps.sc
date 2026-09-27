$input v_texcoord0

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

// Anomaly screen-space ambient occlusion, ported 1:1 from the reference pass
// CRenderTarget::phase_ssao (archive_sourse/Layers/xrRenderPC_R4/
// r4_rendertarget_phase_ssao.cpp:12-105) driving the element 0 of
// CBlender_SSAO_noMSAA (blender_ssao.cpp:16, r_Pass "combine_1" /
// "ssao_calc_nomsaa"), i.e. game_unpacked/shaders/r3/ssao_calc.ps:1-60 with
// calc_ssao() from game_unpacked/shaders/r3/ssao.ps:114-247.
//
// The algorithm is NOT HBAO and NOT HDAO: those are separate opt-in defines
// (r4.cpp:1172-1194, ssao_hbao.ps / ssao_hdao*.ps) which the reference build
// does not enable. The active kernel is the classic CryEngine RINGS x DIRS
// screen-space ring walk of ssao.ps:163-224, dithered per pixel by the jitter
// noise texture jitter0.

// Attachment 1 of the scene FB: the view-space position gbuffer_load_data()
// rebuilds as gbd.P (gbuffer_stage.h:120, :128). The port stores that position
// verbatim instead of reconstructing it from the projected z, so ssao.ps's
// `gbuffer_load_data( tap )` and its `float3 tap_pos = gbd.P` reduce to a plain
// fetch of this target at the tap coordinate.
SAMPLER2D(s_position, 0);
// Attachment 2: Anomaly f_deffer::position (gbuffer_stage.h:7). gbd.N is
// gbuf_unpack_normal(P.xy) (gbuffer_stage.h:131), the first two channels.
SAMPLER2D(s_gbuf, 1);
// jitter0, TEX_jitter = 64 (r2_types.h:118). stdafx.h:54 binds jitter0 to
// JITTER(0) and stdafx.h:60 gives it smp_jitter, i.e. the point/wrap sampler the
// texture is created with on the C++ side. Only jitter0 is read by calc_ssao
// (ssao.ps:156); jitter1..3, jitter4 (the HBAO float noise) and jitterMipped
// belong to the HBAO / SSDO / direct-light paths this port does not run.
SAMPLER2D(s_jitter0, 2);

// ssao_noise_tile_factor, r4_rendertarget_phase_ssao.cpp:40-42
//   2.0f * tan(deg2rad(67.5f)) / tan(deg2rad(Device.fFOV))
// ssao_kernel_size, the same file :44-46
//   150.0f * tan(deg2rad(67.5f)) / tan(deg2rad(Device.fFOV))
// Both are the reference's own expressions; nothing here rescales them. They
// arrive as vec4 (the only scalar shape bgfx uniforms can be fed as, see
// combine_ps.sc u_exposure) and the kernel reads .x, the component the
// reference's float constant holds.
uniform vec4 u_ssao_noise_tile_factor;
uniform vec4 u_ssao_kernel_size;

// u_invView is the bgfx predefined per-view uniform (bgfx_shader.sh), the mirror
// of the AXR m_inv_V / m_v2w that ssao.ps:151 multiplies the view-space
// position with to tile the noise in world space.

// ---------------------------------------------------------------------------
// ssao.ps:35-56, the kernel of SSAO_QUALITY 3 and 4: RINGS 3 rings, DIRS 8
// directions, the four radii and the nine sector boundaries. ps_r_ssao
// (xrRender_console.cpp:96) defaults to 3 and r4_rendertarget.cpp:297-298 caps
// it to 3 whenever the mode is not hdao, and SSAO_QUALITY is that value verbatim
// (r4.cpp:1365-1371). The reference indexes the two arrays with the loop
// counters of the [unroll]ed double loop (ssao.ps:172-173); the loop is written
// out here instead, following the convention of the other .sc files in this
// directory (deferred_light_ps.sc:377-384 spells the 8 PCSS taps out), so every
// radius / angle pair below is a literal of the arrays above.
// ---------------------------------------------------------------------------

// ssao.ps:169-221, one tap: the noise picks the radius inside the ring and the
// angle inside the sector, the tap lands at tc + r * scale, and the occlusion
// accumulates the range-attenuated hemisphere factor of the sample.
void ssao_tap(inout float occ, inout float num_dir, vec3 P, vec3 N, inout vec2 SmallTap,
    float radLo, float radHi, float angLo, float angHi, vec2 scale, vec2 tc)
{
    // ssao.ps:169-171
    SmallTap.x *= 31337.0;
    SmallTap.y *= 73313.0;
    SmallTap = fract(SmallTap);

    // ssao.ps:172-176
    float r = lerp(radLo * 1.3, radHi * 1.3, SmallTap.x);
    float a = lerp(angLo, angHi, SmallTap.y);
    // HLSL sincos(a, s, c) has no single GLSL/SPIR-V spelling, so the pair is
    // written out; s = sin(a), c = cos(a), exactly as ssao.ps:175 asks for.
    float s = sin(a);
    float c = cos(a);
    vec2 tap = vec2(r * c, r * s);
    tap *= scale;
    tap += tc;

    // ssao.ps:190-201 - gbuffer_load_data( tap ).P
    vec3 tap_pos = texture2D(s_position, tap).xyz;

    // ssao.ps:200-204
    vec3 dir = tap_pos - P;
    float dist = length(dir);
    dir = normalize(dir);

    // ssao.ps:205-215
    float infl = saturate(dot(dir, N));
    float occ_factor = saturate(dist);
    float range_att = saturate(1.0 - dist * 0.5);
    occ += (infl + 0.01) * lerp(1.0, occ_factor, infl) * range_att;
    num_dir += (infl + 0.01) * range_att;
}

void main()
{
    // ssao_calc.ps:27-50. The reference reads the G-buffer at the pixel and at
    // the three half-texel neighbours, picks the nearest of the four into `gbd`
    // (ssao_calc.ps:44-47) and then builds P / N out of *gbd0* - the selection
    // result is never read again, so the three extra fetches cannot change the
    // output and are not repeated here. What is left is the centre fetch:
    //   P = float4(gbd0.P, gbd0.mtl)   (position, material in .w)
    //   N = float4(gbd0.N, gbd0.hemi)  (normal, hemi in .w)
    // Only .xyz of both is consumed by calc_ssao, and the tc it is fetched at
    // is I.tc0 (ssao_calc.ps:53), the same coordinate combine_vs.sc hands over.
    vec2 tc = v_texcoord0;
    vec3 P = texture2D(s_position, tc).xyz;
    vec3 N = gbuf_unpack_normal(texture2D(s_gbuf, tc).xy);

    // blender_ssao.cpp:17-18 runs the pass stencil-gated (LESSEQUAL 0xff with
    // ref 0x01), i.e. only the pixels the G-buffer pass marked are shaded; the
    // rest keeps the value r4_rendertarget_phase_ssao.cpp:16-17 cleared the
    // target to. bgfx runs no stencil in this pass, so the same test is made on
    // the data the stencil encodes - the view-space position the geometry
    // writers leave in attachment 1, which stays 0 for sky and cloud because
    // they only ever target attachment 0 (bgfxHDR::BindScene). Writing that
    // clear value out is also what keeps ssao.ps:134's point_depth filter away
    // from the normalize(0) further down, which is the reference's reason for
    // having the stencil at all.
    if (dot(P, P) < 1e-6)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ssao.ps:133-137
    float point_depth = P.z;
    if (point_depth < 0.01)
        point_depth = 100000.0;
    // The 1024x768 the reference hard-codes here is its own virtual resolution,
    // not the frame size, and it is kept as it is: the tap radius has to be the
    // reference's, not one retuned for the current window.
    vec2 scale = vec2(0.5 / 1024.0, 0.5 / 768.0) * u_ssao_kernel_size.x / max(point_depth, 1.3);

    // ssao.ps:151-156 - the noise coordinate: the view-space point through the
    // inverse view matrix (world space), scaled by the tiling factor, with the
    // world height folded into x and z so vertical surfaces do not all land on
    // the same noise texel.
    vec3 tc1 = mul(u_invView, vec4(P, 1.0)).xyz;
    tc1 *= u_ssao_noise_tile_factor.x;
    tc1.xz += tc1.y;
    vec2 SmallTap = texture2D(s_jitter0, tc1.xz).xy;

    // ssao.ps:141-142
    float occ = 0.0;
    float num_dir = 0.0;

    // ssao.ps:163-224, RINGS 3 x DIRS 8 with rads[] = {0.2, 0.57735, 0.8165, 1.0}
    // and angles[] = {0, 0.7854, 1.5708, 2.3562, 3.1416, 3.9267, 4.7124, 5.4978,
    // 6.2832}. Every ring reads rads[rad] and rads[rad+1], every direction
    // angles[dir] and angles[dir+1], so the last ring stops at angles[7..8] and
    // the outermost radius rads[3] is the upper bound of ring 2 - the reference
    // never uses rads[4].
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 0.0000, 0.7854, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 0.7854, 1.5708, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 1.5708, 2.3562, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 2.3562, 3.1416, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 3.1416, 3.9267, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 3.9267, 4.7124, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 4.7124, 5.4978, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.20000, 0.57735, 5.4978, 6.2832, scale, tc);

    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 0.0000, 0.7854, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 0.7854, 1.5708, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 1.5708, 2.3562, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 2.3562, 3.1416, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 3.1416, 3.9267, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 3.9267, 4.7124, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 4.7124, 5.4978, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.57735, 0.81650, 5.4978, 6.2832, scale, tc);

    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 0.0000, 0.7854, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 0.7854, 1.5708, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 1.5708, 2.3562, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 2.3562, 3.1416, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 3.1416, 3.9267, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 3.9267, 4.7124, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 4.7124, 5.4978, scale, tc);
    ssao_tap(occ, num_dir, P, N, SmallTap, 0.81650, 1.00000, 5.4978, 6.2832, scale, tc);

    // ssao.ps:225-246
    occ /= num_dir;
    occ = saturate(occ);
    // ssao.ps:236-240 - the +0.3 lift is the SSAO_QUALITY == 1 branch, which the
    // reference build does not compile in (ps_r_ssao = 3).
    occ = (occ + 0.2) / (1.0 + 0.2);
    // ssao.ps:243-244 - the reference fades the occlusion out over the weapon
    // model in the last 0.1 units of view-space length.
    float WeaponAttenuation = smoothstep(0.8, 0.9, length(P));
    occ = lerp(1.0, occ, WeaponAttenuation);

    // ssao_calc.ps:59. The reference target is D3DFMT_R16F (r4_rendertarget.cpp:861),
    // so only the red channel of this write survives; the other three are the
    // reference's float4(occ, occ, occ, occ) and cost nothing to keep.
    gl_FragColor = vec4(occ, occ, occ, occ);
}
