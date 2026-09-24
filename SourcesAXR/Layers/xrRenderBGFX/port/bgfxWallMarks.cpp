#include "stdafx.h"
#pragma hdrstop

#include "bgfxWallMarks.h"
#include "../bgfxShaderCompiler.h"

#include "../../../xrEngine/IGame_Level.h"
#include "../../../xrEngine/device.h"
#include "../../../xrcdb/Frustum.h"

#include <vector>
#include <cstring>

namespace bgfxWallMarks
{
namespace
{
	const float		kWallmarkTTL	= 50.f;	// ps_r__WallmarkTTL default
	const float		kDistFadeSqr	= 100.f * 100.f;
	const float		kSsaClip	= 3.5f * 0.25f;	// r_ssaDISCARD/4

	struct Vertex
	{
		Fvector		pos;
		u32		color;
		Fvector2	uv;
	};

	struct StaticWallmark
	{
		Fsphere			bounds;
		xr_vector<Vertex>	verts;
		float			ttl;
		bgfx_texture_handle_t	texture;
	};

	// ---- state ----
	xr_vector<StaticWallmark*>	s_pool;
	xr_vector<StaticWallmark*>	s_items;
	xrCriticalSection		s_lock;

	Fvector			s_normal;
	CFrustum		s_clipper;
	sPoly			s_polySrc;
	sPoly			s_polyDst;
	xrXRC			s_xrc;
	CDB::Collector		s_collector;
	xr_vector<u32>		s_adjacency;

	bgfx_program_handle_t	s_prog		= BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t	s_sampler	= BGFX_INVALID_HANDLE;
	bgfx_vertex_layout_t	s_layoutDesc	= {};
	bool			s_layoutReady	= false;

	u32			s_added		= 0;

	// =========================================================================
	// program
	// =========================================================================
	bool EnsureProgram()
	{
		if (bgfxIsValid(s_prog))
			return true;

		if (!s_layoutReady)
		{
			bgfx_vertex_layout_begin(&s_layoutDesc, bgfx_get_renderer_type());
			bgfx_vertex_layout_add(&s_layoutDesc, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			bgfx_vertex_layout_add(&s_layoutDesc, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
			bgfx_vertex_layout_add(&s_layoutDesc, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			bgfx_vertex_layout_end(&s_layoutDesc);
			s_layoutReady = true;
		}

		std::vector<u8> vsBlob, psBlob;
		if (!bgfxShaderCompileFile("wallmark_vs.sc", 'v', vsBlob) ||
		    !bgfxShaderCompileFile("wallmark_ps.sc", 'f', psBlob))
		{
			LogError("[BGFX] Wallmark program build failed");
			return false;
		}
		if (vsBlob.empty() || psBlob.empty())
			return false;

		bgfx_shader_handle_t vsh = bgfx_create_shader(bgfx_copy(vsBlob.data(), (u32)vsBlob.size()));
		bgfx_shader_handle_t fsh = bgfx_create_shader(bgfx_copy(psBlob.data(), (u32)psBlob.size()));
		if (!bgfxIsValid(vsh) || !bgfxIsValid(fsh))
			return false;

		s_prog = bgfx_create_program(vsh, fsh, true);
		if (!bgfxIsValid(s_prog))
		{
			LogError("[BGFX] Wallmark program build failed");
			return false;
		}

		s_sampler = bgfx_create_uniform("s_wallmark", BGFX_UNIFORM_TYPE_SAMPLER, 1);
		LogInfo("[BGFX] Wallmark program created: %u", s_prog.idx);
		return true;
	}

	// =========================================================================
	// pool
	// =========================================================================
	StaticWallmark* static_wm_allocate()
	{
		StaticWallmark* W = nullptr;
		if (s_pool.empty())
			W = xr_new<StaticWallmark>();
		else
		{
			W = s_pool.back();
			s_pool.pop_back();
		}

		W->ttl = kWallmarkTTL;
		W->verts.clear();
		W->texture = BGFX_INVALID_HANDLE;
		return W;
	}

	void static_wm_destroy(StaticWallmark* W)
	{
		s_pool.push_back(W);
	}

	// =========================================================================
	// math (1:1 port of CWallmarksEngine::BuildMatrix/RecurseTri)
	// =========================================================================
	void BuildMatrix(Fmatrix& mView, float invsz, const Fvector& from)
	{
		Fmatrix		mScale;
		Fvector		at, up, right, y;
		at.sub(from, s_normal);
		y.set(0, 1, 0);
		if (_abs(s_normal.y) > .99f) y.set(1, 0, 0);
		right.crossproduct(y, s_normal);
		up.crossproduct(s_normal, right);
		mView.build_camera(from, at, up);
		mScale.scale(invsz, invsz, invsz);
		mView.mulA_43(mScale);
	}

	void RecurseTri(u32 t, Fmatrix& mView, StaticWallmark& W)
	{
		CDB::TRI* T = s_collector.getT() + t;
		if (T->dummy) return;
		T->dummy = 0xffffffff;

		u32*		v_ids = T->verts;
		Fvector*	v_data = s_collector.getV();
		s_polySrc.clear();
		s_polySrc.push_back(v_data[v_ids[0]]);
		s_polySrc.push_back(v_data[v_ids[1]]);
		s_polySrc.push_back(v_data[v_ids[2]]);
		s_polyDst.clear();

		sPoly* P = s_clipper.ClipPoly(s_polySrc, s_polyDst);
		if (P)
		{
			Fvector		UV;
			Vertex		V0, V1, V2;

			mView.transform_tiny(UV, (*P)[0]);
			V0.pos.set((*P)[0]); V0.uv.set((1 + UV.x) * .5f, (1 - UV.y) * .5f); V0.color = 0;
			mView.transform_tiny(UV, (*P)[1]);
			V1.pos.set((*P)[1]); V1.uv.set((1 + UV.x) * .5f, (1 - UV.y) * .5f); V1.color = 0;

			for (u32 i = 2; i < P->size(); i++)
			{
				mView.transform_tiny(UV, (*P)[i]);
				V2.pos.set((*P)[i]); V2.uv.set((1 + UV.x) * .5f, (1 - UV.y) * .5f); V2.color = 0;
				W.verts.push_back(V0);
				W.verts.push_back(V1);
				W.verts.push_back(V2);
				V1 = V2;
			}

			// recurse
			for (u32 i = 0; i < 3; i++)
			{
				u32 adj = s_adjacency[3 * t + i];
				if (0xffffffff == adj) continue;
				CDB::TRI* SML = s_collector.getT() + adj;
				v_ids = SML->verts;

				Fvector test_normal;
				test_normal.mknormal(v_data[v_ids[0]], v_data[v_ids[1]], v_data[v_ids[2]]);
				float cosa = test_normal.dotproduct(s_normal);
				if (cosa < 0.034899f) continue;	// cos(88)
				RecurseTri(adj, mView, W);
			}
		}
	}

	void AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
		bgfx_texture_handle_t texture, float sz)
	{
		// query for polygons in bounding box, calculate adjacency
		{
			Fbox	bb_query;
			Fvector	bbc, bbd;
			bb_query.set(contact_point, contact_point);
			bb_query.grow(sz * 2.5f);
			bb_query.get_CD(bbc, bbd);
			s_xrc.box_options(CDB::OPT_FULL_TEST);
			s_xrc.box_query(g_pGameLevel->ObjectSpace.GetStaticModel(), bbc, bbd);
			u32 triCount = s_xrc.r_count();
			if (0 == triCount)
				return;

			CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
			s_collector.clear();
			s_collector.add_face_packed_D(pVerts[pTri->verts[0]], pVerts[pTri->verts[1]], pVerts[pTri->verts[2]], 0);
			for (u32 t = 0; t < triCount; t++)
			{
				CDB::TRI* T = tris + s_xrc.r_begin()[t].id;
				if (T == pTri) continue;
				s_collector.add_face_packed_D(pVerts[T->verts[0]], pVerts[T->verts[1]], pVerts[T->verts[2]], 0);
			}
			s_collector.calc_adjacency(s_adjacency);
		}

		// calc face normal
		Fvector N;
		N.mknormal(pVerts[pTri->verts[0]], pVerts[pTri->verts[1]], pVerts[pTri->verts[2]]);
		s_normal.set(N);

		// build 3D ortho-frustum
		Fmatrix	mView, mRot;
		BuildMatrix(mView, 1 / sz, contact_point);
		mRot.rotateZ(::Random.randF(deg2rad(-20.f), deg2rad(20.f)));
		mView.mulA_43(mRot);
		s_clipper.CreateFromMatrix(mView, FRUSTUM_P_LRTB);

		// create wallmark
		StaticWallmark* W = static_wm_allocate();
		W->texture = texture;
		RecurseTri(0, mView, *W);

		if (W->verts.size() < 3)
		{
			static_wm_destroy(W);
			return;
		}

		Fbox bb;
		bb.invalidate();
		for (auto& I : W->verts) bb.modify(I.pos);
		bb.getsphere(W->bounds.P, W->bounds.R);

		// search if similar wallmark exists (same texture + almost same center)
		for (auto it = s_items.begin(); it != s_items.end(); ++it)
		{
			StaticWallmark* wm = *it;
			if (wm->texture.idx == W->texture.idx && wm->bounds.P.similar(W->bounds.P, 0.02f))
			{
				static_wm_destroy(wm);
				*it = W;
				return;
			}
		}

		s_items.push_back(W);

		++s_added;
		if (s_added == 1 || (s_added % 100) == 0)
			LogInfo("[BGFX] Wallmark added: verts=%u items=%u tex=%u",
				(u32)W->verts.size(), (u32)s_items.size(), W->texture.idx);
	}

	// =========================================================================
	// submit
	// =========================================================================
	void SubmitBatch(bgfx_texture_handle_t tex, const Vertex* verts, u32 count)
	{
		if (!count || !bgfxIsValid(tex))
			return;

		const u32 maxVerts = 65532;
		static const float s_identity[16] =
		{
			1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f,
		};

		u32 done = 0;
		while (done < count)
		{
			u32 batch = count - done;
			if (batch > maxVerts) batch = maxVerts;
			const u32 nV = batch;
			const u32 nI = batch;

			bgfx_transient_vertex_buffer_t tvb;
			bgfx_transient_index_buffer_t tib;
			if (!bgfx_alloc_transient_buffers(&tvb, &s_layoutDesc, nV, &tib, nI, false))
				return;

			memcpy(tvb.data, verts + done, (size_t)nV * sizeof(Vertex));
			u16* idx = (u16*)tib.data;
			for (u32 i = 0; i < nI; i++) idx[i] = (u16)i;

			// Reference effects\wallmark: SrcBlend=DestColor, DstBlend=SrcColor.
			const uint64_t st = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
				| BGFX_STATE_DEPTH_TEST_LEQUAL
				| BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_SRC_COLOR)
				| BGFX_STATE_MSAA;
			bgfx_set_transform(s_identity, 1);
			bgfx_set_state(st, 0);
			bgfx_set_transient_vertex_buffer(0, &tvb, 0, nV);
			bgfx_set_transient_index_buffer(&tib, 0, nI);
			bgfx_set_texture(0, s_sampler, tex, UINT32_MAX);
			bgfx_submit(0, s_prog, 0, BGFX_DISCARD_ALL);

			done += batch;
		}
	}
} // namespace

// =============================================================================
// public API
// =============================================================================
void AddWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
	bgfx_texture_handle_t texture, float sz)
{
	if (!pTri || !pVerts || !bgfxIsValid(texture))
		return;
	if (!g_pGameLevel)
		return;
	// optimization cheat: don't allow wallmarks more than 100 m from viewer
	if (contact_point.distance_to_sqr(Device.vCameraPosition) > kDistFadeSqr)
		return;

	s_lock.Enter();
	AddWallmark_internal(pTri, pVerts, contact_point, texture, sz);
	s_lock.Leave();
}

void Clear()
{
	s_lock.Enter();
	for (StaticWallmark* W : s_items)
		static_wm_destroy(W);
	s_items.clear();
	for (StaticWallmark* W : s_pool)
		xr_delete(W);
	s_pool.clear();
	s_lock.Leave();
}

void Render()
{
	if (!EnsureProgram() || !bgfxIsValid(s_prog))
		return;
	if (s_items.empty())
		return;

	struct Batch
	{
		bgfx_texture_handle_t	tex;
		xr_vector<Vertex>	verts;
	};
	xr_vector<Batch> batches;

	s_lock.Enter();

	const float dt = Device.fTimeDelta;
	for (auto it = s_items.begin(); it != s_items.end(); )
	{
		StaticWallmark* W = *it;
		const float dst = Device.vCameraPosition.distance_to_sqr(W->bounds.P);
		const float ssa = (dst > 0.0001f) ? (W->bounds.R * W->bounds.R) / dst : 1.f;

		if (ssa >= kSsaClip)
		{
			// static_wm_render: grey mark, alpha = 1 - ttl/TTL (fresh marks
			// are fully applied, ageing ones fade towards neutral grey).
			float a = 1.f - (W->ttl / kWallmarkTTL);
			clamp(a, 0.f, 1.f);
			const u32 aC = (u32)iFloor(a * 255.f);
			const u32 color = (aC << 24) | (128u << 16) | (128u << 8) | 128u;

			u32 bi = (u32)batches.size();
			for (u32 i = 0; i < batches.size(); i++)
				if (batches[i].tex.idx == W->texture.idx) { bi = i; break; }
			if (bi == (u32)batches.size())
			{
				Batch nb;
				nb.tex = W->texture;
				batches.push_back(nb);
			}

			xr_vector<Vertex>& dstv = batches[bi].verts;
			for (const Vertex& v : W->verts)
			{
				Vertex out = v;
				out.color = color;
				dstv.push_back(out);
			}

			W->ttl -= 0.1f * dt;	// visible wallmarks fade much slower
		}
		else
		{
			W->ttl -= dt;
		}

		if (W->ttl <= EPS)
		{
			static_wm_destroy(W);
			*it = s_items.back();
			s_items.pop_back();
		}
		else
		{
			++it;
		}
	}

	for (Batch& b : batches)
		SubmitBatch(b.tex, b.verts.data(), (u32)b.verts.size());

	s_lock.Leave();
}
} // namespace bgfxWallMarks
