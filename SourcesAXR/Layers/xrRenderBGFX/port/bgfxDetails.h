#pragma once

// BGFX detail-objects (grass/clutter) subsystem.
// X-Ray bakes detail objects into level.details (chunk0 header, chunk1 models,
// chunk2 slots). The BGFX layer has no CDetailManager, so this module loads the
// database, decompresses the slots around the camera and submits the expanded
// geometry through the grass program. CPU pipeline mirrors the reference
// (Layers/xrRender DetailManager{,_CACHE,_Decompress}.cpp).
//
// The per-blade constants of the reference vertex shaders and where each one is
// reproduced here. deffer_grass.vs declares two vertex-constant stores:
//
//   float4 exdata[61];     // Terrain Normal [xyz] & Grass alpha [w]   (:8)
//   float4 array[61*4];    // 4 float4 per blade                       (:14)
//
// and reads them through the per-vertex matrix id (deffer_grass.vs:27-34):
//
//   int    i     = v.misc.w;             // mid, DetailManager_VS.cpp:96,106
//   float4 m0    = array[i+0];
//   float4 m1    = array[i+1];
//   float4 m2    = array[i+2];
//   float4 c0    = array[i+3];
//   float4 data  = exdata[i/4];
//
// Slots, values and this port's reproduction:
//
//   array[i+0..2]  three rows of the scaled 3x4 instance matrix
//                  DetailManager_VS.cpp:278-281 / dx10DetailManager_VS.cpp:259-261
//                      (M._11*scale, M._21*scale, M._31*scale, M._41) and the two
//                      following rows. Reproduced exactly: SubmitChunk() fills
//                  c_array[i+0..2] the same way and grass_vs.sc:52-55 rebuilds
//                  P.x = dot(m0, v.pos) etc., deffer_grass.vs:37-41.
//
//   array[i+3].w  Instance.c_hemi - the hemisphere byte of the slot, quantized
//                  DetailManager_Decompress.cpp:307  DS.r_qclr(DS.c_hemi, 15),
//                  stored by DetailManager_VS.cpp:294 / dx10DetailManager_VS.cpp:274
//                  as (c_sun, c_sun, c_sun, c_hemi). Reproduced: c_array[i+3]
//                  carries the same tuple and grass_vs.sc:92-93 applies the
//                  reference clamp(c0.w, 0.05f, 1.0f) (deffer_grass.vs:115).
//
//   array[i+3].xyz c_sun, the sun byte of the slot (DS.r_qclr(DS.c_dir, 15),
//                  DetailManager_Decompress.cpp:308). DEAD IN THE R2 GRASS PATH:
//                  deffer_grass.vs / .ps read only c0.w, and the `ms` term of the
//                  G-buffer uses the global c_sun uniform (deffer_grass.vs:16,
//                  :121-123), not this one. Uploaded for layout fidelity, never
//                  read by the shader.
//
//   exdata.xyz     Instance.normal - the world-space normal of the triangle the
//                  blade stands on, DetailManager_Decompress.cpp:227,242 and
//                  dx10DetailManager_VS.cpp:264. Reproduced: c_exdata[inst].xyz,
//                  consumed by grass_vs.sc:91 (deffer_grass.vs:97) and, in the
//                  force-up shift of deffer_grass.vs:46, by grass_vs.sc:58.
//
//   exdata.w       Instance.alpha, the per-blade fade of the visibility pass
//                  (DetailManager.cpp:379-399, GoToValue in
//                  dx10DetailManager_VS.cpp:242). NOT REPRODUCED: that pass
//                  (r_ssaDISCARD / r_ssaCHEAP, the distance fade and the alpha
//                  easing) does not exist in this port, which culls whole slots
//                  by distance instead, so every submitted blade has the alpha
//                  the reference gives a fully faded-in one. The consumer of the
//                  value, the screen-space dither clip of deffer_grass.ps:91-96
//                  (clip(I.M1.x - DITHER_THRESHOLDS[dither_idx])), is therefore
//                  not ported either - with alpha at its steady-state 1 it never
//                  clips, and inventing a fade would not be the reference.
//
//   v_detail.misc  (common_iostructs.h:273) - (u(Q), v(Q), frac, mid). u and v
//                  are the 16-bit quantised coordinates of DetailManager_VS.cpp:
//                  103-104, frac the height fraction of :105, mid the matrix id
//                  of :96. All four are carried (OutVertex u/v/frac/mid) and the
//                  quantisation is DetailManager_VS.cpp:37-41 verbatim.
//
// Not reproduced, and deliberately not faked with a constant:
//
//   * the wave. deffer_grass.vs:48-58 needs the per-frame `consts` / `wave` /
//     `dir2D` stores of DetailManager_VS.cpp:198-220 and the still/wave0/wave1
//     submission split of the same function (var_id 0/1/2, chosen per blade by
//     DetailManager_Decompress.cpp:320-325); the still pass is a different
//     shader element (details_lod.s, the `lod` program), which this port does not
//     have. grass_vs.sc keeps the previous standalone wave and says so.
//   * the flora fake-up normal of deffer_grass.ps:70-79, which needs the
//     `s_bump` map of details_blend.s:14 (`<t_base>_bump`); the shipped level
//     data has no such file, so there is nothing to sample.
//   * USE_R2_STATIC_SUN's ms term (deffer_grass.vs:121-123) and the def_aref
//     alpha-test threshold (deffer_grass.ps:46) are R4 per-material constants
//     this port does not bind; the class keeps its own u_grassAlpha stand-in.

void	bgfxDetailsLoad();
void	bgfxDetailsUnload();
void	bgfxDetailsRender();
