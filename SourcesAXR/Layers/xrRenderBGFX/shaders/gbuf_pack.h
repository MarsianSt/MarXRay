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
// Hemi sources in the reference:
//   static / decal  deffer_base_flat.ps:37      h = I.position.w = v_static.Nh.w (vertex)
//                  deffer_base_flat.vs:16       O.position = float4(Pe, I.Nh.w)
//   models          deffer_model_flat.vs:19-30 hemi cube, sat(dot(face, abs(Nw)))
//   grass           deffer_grass.vs:115         clamp(c0.w, 0.05f, 1.0f) (per-blade const)
//   particle        deffer_particle.vs:22       0.2h (constant)
// The lightmap alternative (USE_LM_HEMI, deffer_base_flat.ps:23-26 with
// get_hemi() = lm.a, common_functions.h:129-137) and the per-blade/hemi-cube
// constant arrays are not bound by this port - no lightmaps are loaded and the
// R4 constant buffers are absent - so stage 1 feeds the environment hemi colour
// (the same source calc_model_hemi_r1() uses, common_functions.h:99-101) to
// everything but particles, which keep the AXR constant. Stage 2 replaces this.
uniform vec4 u_gbufHemi;

#endif // GBUF_PACK_H_HEADER_GUARD
