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

// gbuffer_stage.h:97-112 pack_gbuffer(), Target0 slice only:
//
//   res.position = float4( gbuf_pack_normal( norm.xyz ), pos.z,
//                          gbuf_pack_hemi_mtl( norm.w, pos.w ) );
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
// .w: the reference stores gbuf_pack_hemi_mtl() (gbuffer_stage.h:68-81), which
// bit-crafts an 8-bit hemi and a 5-bit material id into the fp32 exponent
// (MUST_BE_SET 0x40000000, common_defines.h:24-27). The value that comes out of
// that arithmetic is 2^-119-scale, a denormal an RGBA16F attachment flushes to
// zero, so the bit pattern cannot survive the attachment. Stage 1 stores the
// plain saturated hemi in .w and keeps the material id as the per-program
// constant GBUF_MTL (see below); gbuf_unpack_hemi() therefore degrades to an
// identity on the .w the port writes.
vec4 gbuf_pack_gbuffer(vec3 norm, float viewZ, float hemi)
{
    return vec4(gbuf_pack_normal(norm), viewZ, saturate(hemi));
}

// gbuffer_stage.h:83-86, kept for the debug view: the reference reads back the
// hemi byte it packed into .w; the port writes the plain value, so this is the
// identity on it.
float gbuf_unpack_hemi(float mtl_hemi)
{
    return mtl_hemi;
}

// Material id, resolved per draw class by the reference:
//   xmaterial = float(1.0h/4.h) when USE_R2_STATIC_SUN is defined (common.h:17-18),
//   otherwise float(L_material.w) (common.h:20) - an R4 constant this port does
//   not bind, hence the 0.25 static value;
//   MAT_FLORA = 0.15f (common_brdf.h:16) for grass (deffer_grass.ps:99-102);
//   0.95f for terrain (deffer_terrain_mid_flat.ps:56, deffer_terrain_low_flat.ps:22,
//   deffer_impl_flat.ps:210);
//   0 for LOD geometry (lod.ps:103);
//   deffer_particle.ps:66 and the aref decals (deffer_base_aref_flat.ps:92) use
//   xmaterial like the other static geometry.
// Every G-buffer writer below exposes it as GBUF_MTL for stage 3.
//
// Hemi, the .w every writer below packs. In the reference it is a bare 0..1
// scalar, not a colour: gbuf_unpack_hemi (gbuffer_stage.h:83-86) divides the
// packed byte by 254.8, and hmodel.h:109 spends it as
//   float hscale = h;  ->  env_d *= light.xxx  (hmodel.h:125)
// i.e. it is the scale of the ambient term, with the colour applied on top by
// the caller. Where the reference gets it per draw class:
//   static / terrain  deffer_base_flat.vs:25     O.position = float4(Pe, I.Nh.w)
//                     deffer_terrain_flat_d.vs:19 same, read back as D.w in
//                                               deffer_terrain_mid_flat.ps:56
//                     -> per vertex, baked into the level mesh
//   models            deffer_base_flat.ps:29-40 the hemi-cube block
//                     (deffer_model_flat.vs:19-30, sat(dot(face, abs(Nw)))) is
//                     commented out, so this class also reads I.position.w
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
// The two classes left on the normal-derived up factor are the ones whose
// reference source is a CPU-side value the port has no binding for:
//   grass    deffer_grass.vs:115 clamp(c0.w, 0.05f, 1.0f) - c0 is
//            array[i+3] of the per-batch 61*4-float4 `array` constant the
//            detail manager dumps (DetailManager_VS.cpp:174,213), and the
//            port's grass layout carries no such data (bgfxDetails.cpp:
//            424-427)
//   models   deffer_model_flat.vs:18-25 the hemi cube, whose faces are R4
//            constants filled per object from CROS_impl::get_hemi_cube()
//            (R_Backend_hemi.cpp:16-26) and only faked here
//            (bgfxRenderInterface.h:150)
// Wallmarks are the same story: the decal PS reads I.position.w
// (deffer_base_aref_flat.ps:83) and the port's mark quad has no Nh.
//
// The up factor itself is the same 1:1 substitution the reference's own
// fallback makes, calc_model_hemi_r1's max(0, norm_w.y): 1.0 facing up and 0.0
// facing down, per pixel, in world space.
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

#endif // GBUF_PACK_H_HEADER_GUARD
