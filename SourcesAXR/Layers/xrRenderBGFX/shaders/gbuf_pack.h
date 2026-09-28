#ifndef GBUF_PACK_H_HEADER_GUARD
#define GBUF_PACK_H_HEADER_GUARD

// Anomaly deferred G-buffer packing, ported from
// game_unpacked/shaders/r3/gbuffer_stage.h. Only the parts the bgfx scene
// attachments need live here - the R4 C++ that fed the reference is not part
// of this repository.
//
// The .sc files are compiled to SPIR-V for the Vulkan backend
// (bgfxShaderCompiler.cpp:36-39 GetTargetForBackend), so everything below is
// written in the GLSL subset the other .sc files use (vec4/texture2D/mul/
// gl_FragData) and avoids HLSL-only intrinsics. The .sc files are parsed as
// HLSL, not GLSL, so the two-argument atan of GLSL is spelled atan2 as in the
// reference and sincos is written out as sin/cos.

// gbuffer_stage.h:51-65, the active branch: an octahedral-free azimuth/elevation
// pair, the normal in the first two channels of f_deffer::position
// (gbuffer_stage.h:7). The INTERNAL_AMD variant above it (gbuffer_stage.h:29-49)
// is a second encoding that only the AMD path selected and is not ported.
vec2 gbuf_pack_normal(vec3 norm)
{
    return float2(atan2(norm.y, norm.x) / 3.14159f, norm.z);
}

// gbuffer_stage.h:59-65. HLSL sincos() has no GLSL/SPIR-V spelling, so the two
// calls are written out; the result is identical to sincos(x, theta.x, theta.y)
// with theta.x = sin, theta.y = cos.
vec3 gbuf_unpack_normal(vec2 norm)
{
    float s = sin(norm.x * 3.14159f);
    float c = cos(norm.x * 3.14159f);
    float2 phi = vec2(sqrt(1.0f - norm.y * norm.y), norm.y);
    return vec3(c * phi.x, s * phi.x, phi.y);
}

// common_functions.h:115 unpack_bx2(), the decoder the reference applies to the
// D3DCOLOR normal of the AXR v_static.Nh semantic (deffer_base_flat.vs:13-14
// reads unpack_D3DCOLOR(I.Nh) then unpack_bx2(I.Nh)). unpack_D3DCOLOR() itself
// is only a bgra swizzle (common_functions.h:114): the D3DCOLOR bytes are copied
// into the vertex attribute unchanged by the CPU repack in
// bgfxRenderCompat.cpp, so a_normal.xyz already sits in the reference's channel
// order and the swizzle is a no-op here.
vec3 unpack_bx2(vec3 v)
{
    return 2.0 * v - 1.0;
}

// gbuffer_stage.h:68-81 gbuf_pack_hemi_mtl, verbatim, in the GLSL subset the .sc
// files are written in. The reference bit-crafts an 8-bit hemi and a 5-bit
// material id into the fp32 bit pattern and stores that as a float:
//
//   uint packed_mtl = uint((mtl / 1.333333333) * 31.0);
//   uint packed = MUST_BE_SET + (uint(saturate(hemi) * 255.9) << 13)
//                          + ((packed_mtl & uint(31)) << 21);
//   if ((packed & USABLE_BIT_13) == 0) packed |= USABLE_BIT_14;
//   if (packed_mtl & uint(16))          packed |= USABLE_BIT_15;
//   return asfloat(packed);
//
// MUST_BE_SET is 0x40000000 (common_defines.h:27), which is 2.0 in fp32 - an
// ordinary number in the 2..4 range, not a denormal. hemi<<13 lands in mantissa
// bits 13..20 and mtl<<21 in bits 21..25, and the FP16 render target the
// reference declares in its own comment (common_defines.h:24-25) keeps the top 10
// mantissa bits while its 5 exponent bits absorb the rest, which is exactly why
// those two shifts are 13 and 21: the 8 hemi bits and 2 of the mtl bits survive
// the fp16 conversion and the low 3 mtl bits ride in the exponent, with
// USABLE_BIT_15 (the fp32 sign bit) carrying mtl's 5th bit and USABLE_BIT_14
// forcing a non-zero mantissa. The two decoders below read them back out of the
// very same shifts, so the pair round-trips on the same target the port uses
// (bgfxHDR.cpp:2146, s_gbufFormat = RGBA16F).
const uint MUST_BE_SET   = uint(0x40000000);
const uint USABLE_BIT_13 = uint(0x02000000);
const uint USABLE_BIT_14 = uint(0x04000000);
const uint USABLE_BIT_15 = uint(0x80000000);

float gbuf_pack_hemi_mtl(float hemi, float mtl)
{
    uint packed_mtl = uint((mtl / 1.333333333) * 31.0);
    uint packed = MUST_BE_SET + (uint(saturate(hemi) * 255.9) << 13)
                + ((packed_mtl & uint(31)) << 21);
    if ((packed & USABLE_BIT_13) == uint(0))
        packed |= USABLE_BIT_14;
    if (packed_mtl & uint(16))
        packed |= USABLE_BIT_15;
    return uintBitsToFloat(packed);
}

// gbuffer_stage.h:83-86 and :88-93, the two decoders gbuffer_load_data() calls
// at :134 and :137 - the `P.w` combine_1.ps:106/107 splits into `mtl` and `hemi`.
// Both are the reference's own bit arithmetic on the same uint the packer built.
float gbuf_unpack_hemi(float mtl_hemi)
{
    return float((floatBitsToUint(mtl_hemi) >> 13) & uint(255)) * (1.0 / 254.8);
}

float gbuf_unpack_mtl(float mtl_hemi)
{
    uint packed = floatBitsToUint(mtl_hemi);
    uint packed_hemi = ((packed >> 21) & uint(15))
                     + (((packed & USABLE_BIT_15) == uint(0)) ? uint(0) : uint(16));
    return float(packed_hemi) * (1.0 / 31.0) * 1.333333333;
}

// gbuffer_stage.h:97-112 pack_gbuffer(), Target0 slice only:
//
//   res.position = float4( gbuf_pack_normal( norm.xyz ), pos.z,
//                          gbuf_pack_hemi_mtl( norm.w, pos.w ) );
//   res.C        = col;
//
// .z is the reference `pos.z`, i.e. the *view-space* z every AXR deffer vertex
// shader builds as I.position = float4(Pe, hemi) with Pe = mul(m_WV, I.P)
// (deffer_base_flat.vs:16, deffer_model_flat.vs:14, deffer_grass.vs:120,
// deffer_particle.vs:21). It is not a projected NDC depth: the reference
// reconstructs the full view position from it in gbuffer_load_data()
// (gbuffer_stage.h:128) and fogs on its length (combine_1.ps:196-197), and
// def_virtualh/2 (common_defines.h:9) offsets the position by the normal there.
// The port therefore reuses the view-space position it already writes into
// attachment 1, so stage 3 can read .z/.xyz out of this attachment and drop
// attachment 1 without losing a bit.
//
// .w is gbuf_pack_hemi_mtl(norm.w, pos.w) above, the reference's own packing.
vec4 gbuf_pack_gbuffer(vec3 norm, float viewZ, float hemi, float mtl)
{
    return vec4(gbuf_pack_normal(norm), viewZ, gbuf_pack_hemi_mtl(hemi, mtl));
}

// The three-argument form the writers that were written before the material id
// reached the G-buffer still call. It forwards with xmaterial, which is the value
// the reference gives those classes anyway: `float ms = xmaterial`
// (deffer_base_flat.ps:21, deffer_base_aref_flat.ps:72, deffer_particle.ps:66),
// i.e. float(1.0h/4.h) under USE_R2_STATIC_SUN (common.h:17-18). It is a
// compatibility shim, not the general case - a class with a different id must
// pass it, and grass_ps.sc is the one class that has to (0.15,
// deffer_grass.ps:101). Every writer in this port is expected to migrate to the
// four-argument form; this overload exists so a class that has not yet does not
// break the whole render.
vec4 gbuf_pack_gbuffer(vec3 norm, float viewZ, float hemi)
{
    return gbuf_pack_gbuffer(norm, viewZ, hemi, 0.25);
}

// Material id, resolved per draw class by the reference:
//   xmaterial = float(1.0h/4.h) when USE_R2_STATIC_SUN is defined (common.h:17-18),
//   otherwise float(L_material.w) (common.h:20) - an R4 constant this port does
//   not bind, hence the 0.25 static value;
//   MAT_FLORA = 0.15f (common_brdf.h:16) for grass (deffer_grass.ps:99-102);
//   0.95f for terrain (deffer_terrain_low_flat.ps:24, deffer_terrain_mid_flat.ps:
//   57, deffer_impl_flat.ps:210);
//   0 for LOD geometry (lod.ps:103);
//   deffer_particle.ps:66 and the aref decals (deffer_base_aref_flat.ps:92) use
//   xmaterial like the other static geometry.
// Every G-buffer writer passes it as the fourth argument of
// gbuf_pack_gbuffer() above, and the resolve reads it back with
// gbuf_unpack_mtl() (gbuffer_stage.h:88-93, gbuffer_stage.h:134) - so the sun
// lobe, the ambient BRDF and the flora/terrain tests all see the class they see
// in the reference, instead of one frame-wide constant.
//
// Hemi, the .w every writer packs. In the reference it is a bare 0..1
// scalar, not a colour: gbuf_unpack_hemi (gbuffer_stage.h:83-86) divides the
// packed byte by 254.8, and hmodel.h:109 spends it as
//   float hscale = h;  ->  env_d *= light.xxx  (hmodel.h:125)
// i.e. it is the scale of the ambient term, with the colour applied on top by
// the caller. Where the reference gets it per draw class:
//   static / terrain  deffer_base_flat.vs:25     O.position = float4(Pe, I.Nh.w)
//                     deffer_terrain_flat_d.vs:19 same, read back as D.w in
//                                               deffer_terrain_mid_flat.ps:53
//                     -> per vertex, baked into the level mesh
//   models            deffer_model_flat.vs:17-25 the hemi-cube block, LIVE in
//                     the reference (sat(dot(hc_mixed, abs(Nw)))), fed from
//                     CROS_impl::get_hemi_cube() - no CROS on this side yet, so
//                     the class uses gbuf_calc_hemi below; see the block at the
//                     end of this file
//   grass             deffer_grass.vs:115        clamp(c0.w, 0.05f, 1.0f) - a
//                                                per-blade constant, flagged
//                                                there as "Some spots are
//                                                bugged (Full black)"
//   particle          deffer_particle.vs:22      .2h, a flat constant
//   with lightmaps    deffer_base_flat.ps:22-27  get_hemi(s_hemi) (USE_LM_HEMI)
//
// None of those sources is a normal-derived quantity, and the ones that are
// reachable are now bound: static and terrain take the per-vertex I.Nh.w byte
// (world_solid_ps.sc / world_terrain_ps.sc, the byte a_normal.w already carries
// - bgfxRenderCompat.cpp:1298-1308), particles take the flat 0.2
// (particle_ps.sc), and the level lightmap still wins over both through
// u_lmapValid.x (deffer_base_flat.ps:22-27). What every one of them multiplies
// is the descriptor's hemi colour, i.e.
// calc_model_hemi_r1 (common_functions.h:99-101):
//
//   float3 calc_model_hemi_r1( float3 norm_w )
//   { return max(0,norm_w.y)*L_hemi_color; }
//
// so the colour factor belongs to the caller - which is exactly where the
// resolve already has it, deferred_light_ps.sc `cubeD * u_hemiColor.rgb * hemi
// + u_ambient.rgb`, CEnvDescriptor::hemi_color = L_hemi_color.
//
// The two classes left on a normal-derived factor are the ones whose reference
// source is a CPU-side value the port carries per draw instead:
//   grass    deffer_grass.vs:115 clamp(c0.w, 0.05f, 1.0f) - c0 is
//            array[i+3] of the per-batch 61*4-float4 `array` constant the
//            detail manager dumps (DetailManager_VS.cpp:174,213), and the
//            port's grass layout carries no such data (bgfxDetails.cpp:
//            424-427)
//   models   deffer_model_flat.vs:17-25 the hemi cube, whose faces are R4
//            constants filled per object from CROS_impl::get_hemi_cube()
//            (R_Backend_hemi.cpp:18-25, Blender_Recorder_StandartBinding.cpp:
//            45-58/:938-939); no CROS equivalent exists on this side, so the
//            class keeps the up factor below - see the block at the end of this
//            file for the whole shape and what it needs
// Wallmarks are the same story: the decal PS reads I.position.w
// (deffer_base_aref_flat.ps:83) and the port's mark quad has no Nh.
//
// The reference computes norm_w in the lighting pass, where the normal is
// already unprojected (hmodel.h:47-48 mul(m_inv_V, normal)). The writers only
// hold v_viewNormal, so the same unprojection happens here: u_invView is the
// bgfx predefined per-view matrix, the mirror of AXR m_inv_V, and it is the very
// one the resolve multiplies the G-buffer normal with (deferred_light_ps.sc,
// hmodel.h:48) - both sides therefore see the same vector.
float gbuf_calc_hemi(vec3 viewNormal)
{
    vec3 nw = mul(u_invView, vec4(normalize(viewNormal), 0.0)).xyz;
    return max(nw.y, 0.0);
}

// The reference's model-class hemi cube (deffer_model_flat.vs:17-25) is NOT
// ported here, and the shape it needs is recorded rather than faked:
//
//   float3 Nw       = mul((float3x3)m_W, (float3)I.N);   // WORLD normal
//   float3 hc_pos   = (float3)hemi_cube_pos_faces;
//   float3 hc_neg   = (float3)hemi_cube_neg_faces;
//   float3 hc_mixed = (Nw < 0) ? hc_neg : hc_pos;         // componentwise
//   float  hemi_val = dot(hc_mixed, abs(Nw));
//   hemi_val        = saturate(hemi_val);
//   O.position      = float4(Pe, hemi_val);
//
// Two things have to exist before that block can be written, and neither does:
//
// 1. The two face vectors. hemi_cube_pos_faces / hemi_cube_neg_faces are the
//    r_Constant bindings of Blender_Recorder_StandartBinding.cpp:938-939, filled
//    per object by CROS_impl::get_hemi_cube() (LightTrack.cpp:162-178:accum_hemi,
//    :181-287:update, :345-368:update_smooth) and pushed through
//    R_hemi::set_pos_faces / set_neg_faces (R_Backend_hemi.cpp:18-25). CROS_impl
//    lives in the xrRender layer (LightTrack.cpp, R_Backend_hemi.cpp), which
//    xrRenderBGFX does not build, so there is no object-specific light track on
//    this side at all. The port's stand-in is bgfxRenderInterface.h:150,
//    m_hemi_cube = {1,1,1,1,1,1} - and read through the expression above that is
//    saturate(|x|+|y|+|z|) = 1.0 for every normal, i.e. a constant, not a
//    measurement. Binding that pair would be the fudge this port is not allowed
//    to ship, so the block stays out until a CROS equivalent exists.
//
// 2. The vertex stage. The reference evaluates it PER VERTEX (it is the .vs, and
//    the value rides in O.position.w like every other class's hemi), so a
//    faithful port needs a per-vertex world normal through m_W - a varying the
//    port has no writer for - and the cube select with it. Doing it per pixel
//    off v_viewNormal would be a different function: the interpolated normal is
//    not the vertex normal the reference dots, and abs() of an interpolated
//    component can cross zero inside the triangle.
//
// The model class therefore keeps the normal-derived substitute gbuf_calc_hemi
// above, which is the same thing calc_model_hemi_r1 (common_functions.h:99-101,
// max(0, norm_w.y)) is: the ambient up factor the descriptor's hemi colour is
// built from, i.e. the reference's own fallback shape, not a stand-in for the
// cube. Porting the cube is a separate stage with its own C++ side.

#endif // GBUF_PACK_H_HEADER_GUARD
