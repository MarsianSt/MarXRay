#include "stdafx.h"
#pragma hdrstop

#include "bgfxDetails.h"
#include "DetailFormat.h"
#include "../bgfx_capi.h"
#include "../bgfxShaderCompiler.h"
#include "../bgfxUIShader.h"
#include "../bgfxUIProgram.h"

#include "../../../xrEngine/IGame_Level.h"
#include "../../../xrEngine/IGame_Persistent.h"
#include "../../../xrEngine/device.h"
#include "../../../xrEngine/environment.h"
#include "../../../xrEngine/defines.h"
#include "../../../xrEngine/gamemtllib.h"

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <climits>
#include <cmath>

extern ENGINE_API Fvector4 ps_ssfx_grass_interactive;
extern ENGINE_API Fvector4 ps_ssfx_int_grass_params_1;
extern ENGINE_API int ps_ssfx_terrain_grass_align;
extern ENGINE_API float ps_ssfx_terrain_grass_slope;

namespace
{
	const u32	kMaxVertsPerSubmit	= 60000;
	const u32	kMaxIndicesPerSubmit	= 65532;
	const int	kMaxBenders		= 16;
	const int	kMaxObjects		= 16;
	const int	kMaxDecompressPerFrame	= 24;

	struct DetailVertexIn
	{
		Fvector	P;
		float	u, v;
	};

	struct DetailModel
	{
		flags32		flags;
		float		fMinScale;
		float		fMaxScale;
		u32		number_vertices;
		u32		number_indices;
		DetailVertexIn*	vertices;
		u16*		indices;
		Fbox		bv_bb;
		Fsphere		bv_sphere;
		xr_string	texture;

		DetailModel() : fMinScale(1.f), fMaxScale(1.f),
			number_vertices(0), number_indices(0), vertices(nullptr), indices(nullptr)
		{
			flags.zero();
			bv_bb.invalidate();
			bv_sphere.P.set(0, 0, 0);
			bv_sphere.R = 0.f;
		}
		~DetailModel()
		{
			if (vertices) xr_free(vertices);
			if (indices) xr_free(indices);
		}
	};

	struct SlotItem
	{
		Fmatrix	M;
		float	scale;
		float	c_hemi;
		float	c_sun;
	};

	struct Decompressed
	{
		bool			empty;
		Fvector			center;
		float			radius;
		u8			partId[4];
		xr_vector<SlotItem>	items[4];

		Decompressed() : empty(true), radius(0.f)
		{
			center.set(0, 0, 0);
			partId[0] = partId[1] = partId[2] = partId[3] = DetailSlot::ID_Empty;
		}
	};

	// ---- state ----
	IReader*			s_fs		= nullptr;
	bool				s_loaded	= false;
	DetailHeader			s_header;
	xr_vector<DetailModel*>		s_objects;
	xr_vector<DetailSlot>		s_slots;
	int				s_dither[16][16];
	std::unordered_map<u64, Decompressed>	s_cache;
	xr_vector<u64>			s_pending;
	int				s_camSX		= INT_MIN;
	int				s_camSZ		= INT_MIN;
	u32				s_dmSize	= 0;
	float				s_dmFade	= 0.f;

	float				s_density	= 0.6f;
	float				s_detailScale	= 1.f;
	int				s_radius	= 49;

	// ---- bgfx program/uniforms ----
	bgfx_program_handle_t		s_prog		= BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t		s_sampler	= BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t		s_alphaCtrl	= BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t		s_grassParams	= BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t		s_grassInt	= BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t		s_bendersPos	= BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t		s_bendersSetup	= BGFX_INVALID_HANDLE;
	bgfx_vertex_layout_t		s_layoutDesc	= {};
	bgfx_vertex_layout_handle_t	s_layout	= BGFX_INVALID_HANDLE;
	bool				s_layoutReady	= false;

	struct TexEntry { xr_string name; bgfx_texture_handle_t handle; };
	xr_vector<TexEntry>		s_textures;

	// =========================================================================
	// decompression (mirror of DetailManager{,_Decompress}.cpp)
	// =========================================================================
	void bwdithermap(int levels, int magic[16][16])
	{
		static int magic4x4[4][4] =
		{
			{  0, 14,  3, 13 },
			{ 11,  5,  8,  6 },
			{ 12,  2, 15,  1 },
			{  7,  9,  4, 10 }
		};
		float N = 255.0f / (levels - 1);
		float magicfact = (N - 1) / 16;
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				for (int k = 0; k < 4; k++)
					for (int l = 0; l < 4; l++)
						magic[4 * k + i][4 * l + j] =
						(int)(0.5 + magic4x4[i][j] * magicfact +
							(magic4x4[k][l] / 16.) * magicfact);
	}

	float Interpolate(float* base, u32 x, u32 y, u32 size)
	{
		float f = float(size);
		float fx = float(x) / f; float ifx = 1.f - fx;
		float fy = float(y) / f; float ify = 1.f - fy;
		float c01 = base[0] * ifx + base[1] * fx;
		float c23 = base[2] * ifx + base[3] * fx;
		float c02 = base[0] * ify + base[2] * fy;
		float c13 = base[1] * ify + base[3] * fy;
		float cx = ify * c01 + fy * c23;
		float cy = ifx * c02 + fx * c13;
		return (cx + cy) / 2;
	}

	bool InterpolateAndDither(float* alpha255, u32 x, u32 y, u32 sx, u32 sy, u32 size, int dither[16][16])
	{
		if (size == 0)
			return false;
		clamp(x, (u32)0, size - 1);
		clamp(y, (u32)0, size - 1);
		int c = iFloor(Interpolate(alpha255, x, y, size) + .5f);
		clamp(c, 0, 255);
		u32 row = (y + sy) % 16;
		u32 col = (x + sx) % 16;
		return c > dither[col][row];
	}

	DetailSlot& QueryDB(int sx, int sz)
	{
		static DetailSlot s_empty;
		int db_x = sx + s_header.offs_x;
		int db_z = sz + s_header.offs_z;
		if ((db_x >= 0) && (db_x < int(s_header.size_x)) &&
		    (db_z >= 0) && (db_z < int(s_header.size_z)))
		{
			u32 linear_id = u32(db_z) * s_header.size_x + u32(db_x);
			if (linear_id < s_slots.size())
				return s_slots[linear_id];
		}
		s_empty.w_id(0, DetailSlot::ID_Empty);
		s_empty.w_id(1, DetailSlot::ID_Empty);
		s_empty.w_id(2, DetailSlot::ID_Empty);
		s_empty.w_id(3, DetailSlot::ID_Empty);
		return s_empty;
	}

	bool RaycastGround(const Fvector& from, float bottomY, float& outY, Fvector& outN)
	{
		if (!g_pGameLevel)
			return false;
		Fvector dir;
		dir.set(0.f, -1.f, 0.f);
		float range = from.y - bottomY;
		if (range <= 0.f)
			return false;
		collide::rq_result RQ;
		if (!g_pGameLevel->ObjectSpace.RayPick(from, dir, range, collide::rqtStatic, RQ, nullptr))
			return false;

		outY = from.y - RQ.range;
		if (RQ.O)
			outN.set(0.f, 1.f, 0.f);
		else
		{
			CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + RQ.element;
			Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();
			outN.mknormal(verts[T->verts[0]], verts[T->verts[1]], verts[T->verts[2]]);
		}
		return true;
	}

	void DecompressSlot(int sx, int sz, Decompressed& D)
	{
		D.empty = true;
		DetailSlot& DS = QueryDB(sx, sz);
		if ((DS.id0 == DetailSlot::ID_Empty) && (DS.id1 == DetailSlot::ID_Empty) &&
		    (DS.id2 == DetailSlot::ID_Empty) && (DS.id3 == DetailSlot::ID_Empty))
			return;

		Fbox box;
		box.min.set(sx * DETAIL_SLOT_SIZE, DS.r_ybase(), sz * DETAIL_SLOT_SIZE);
		box.max.set(box.min.x + DETAIL_SLOT_SIZE, DS.r_ybase() + DS.r_yheight(), box.min.z + DETAIL_SLOT_SIZE);
		box.grow(EPS_L);
		box.getsphere(D.center, D.radius);

		float alpha255[4][4];
		for (int i = 0; i < 4; i++)
		{
			alpha255[i][0] = 255.f * float(DS.palette[i].a0) / 15.f;
			alpha255[i][1] = 255.f * float(DS.palette[i].a1) / 15.f;
			alpha255[i][2] = 255.f * float(DS.palette[i].a2) / 15.f;
			alpha255[i][3] = 255.f * float(DS.palette[i].a3) / 15.f;
		}

		float density = s_density;
		float jitter = density / 1.7f;
		u32 d_size = iCeil(DETAIL_SLOT_SIZE / density);
		if (d_size == 0)
			d_size = 1;
		svector<int, 4> selected;

		u32 p_rnd = u32(sx * sz);
		CRandom r_selection(0x12071980 ^ p_rnd);
		CRandom r_jitter(0x12071980 ^ p_rnd);
		CRandom r_yaw(0x12071980 ^ p_rnd);
		CRandom r_scale(0x12071980 ^ p_rnd);

		for (u32 z = 0; z <= d_size; z++)
		{
			for (u32 x = 0; x <= d_size; x++)
			{
				u32 shift_x = r_jitter.randI(16);
				u32 shift_z = r_jitter.randI(16);
				selected.clear();

				if ((DS.id0 != DetailSlot::ID_Empty) && InterpolateAndDither(alpha255[0], x, z, shift_x, shift_z, d_size, s_dither)) selected.push_back(0);
				if ((DS.id1 != DetailSlot::ID_Empty) && InterpolateAndDither(alpha255[1], x, z, shift_x, shift_z, d_size, s_dither)) selected.push_back(1);
				if ((DS.id2 != DetailSlot::ID_Empty) && InterpolateAndDither(alpha255[2], x, z, shift_x, shift_z, d_size, s_dither)) selected.push_back(2);
				if ((DS.id3 != DetailSlot::ID_Empty) && InterpolateAndDither(alpha255[3], x, z, shift_x, shift_z, d_size, s_dither)) selected.push_back(3);
				if (selected.empty())
					continue;

				u32 index = (selected.size() == 1) ? u32(selected[0]) : u32(selected[r_selection.randI(selected.size())]);
				u8 objId = DS.r_id(index);
				if (objId >= s_objects.size())
					continue;
				DetailModel& dobj = *s_objects[objId];

				float rx = (float(x) / float(d_size)) * DETAIL_SLOT_SIZE + box.min.x;
				float rz = (float(z) / float(d_size)) * DETAIL_SLOT_SIZE + box.min.z;

				Fvector itemP;
				itemP.set(rx + r_jitter.randFs(jitter), box.max.y, rz + r_jitter.randFs(jitter));

				Fvector normal;
				normal.set(0.f, 1.f, 0.f);
				float y = box.min.y - 5.f;
				float hitY;
				Fvector hitN;
				if (RaycastGround(itemP, box.min.y - 5.f, hitY, hitN))
				{
					y = hitY;
					normal = hitN;
				}

				Fvector down;
				down.set(0.f, -1.f, 0.f);
				float dotp = normal.dotproduct(down);
				if (dotp > -(1.0f - ::Random.randF(ps_ssfx_terrain_grass_slope * 0.8f, ps_ssfx_terrain_grass_slope)))
					continue;
				if (y < box.min.y)
					continue;

				itemP.y = y;

				SlotItem it;
				it.scale = r_scale.randF(dobj.fMinScale * 0.5f, dobj.fMaxScale * 0.9f);
				it.scale *= s_detailScale;
				it.M.rotateY(r_yaw.randF(0.f, PI_MUL_2));

				if (ps_ssfx_terrain_grass_align > 0)
				{
					Fmatrix curr = it.M;
					it.M.j.set(normal);
					Fvector::generate_orthonormal_basis(it.M.j, it.M.i, it.M.k);
					it.M.mulB_43(curr);
				}
				it.M.translate_over(itemP);
				it.c_hemi = DS.r_qclr(DS.c_hemi, 15);
				it.c_sun = DS.r_qclr(DS.c_dir, 15);

				D.items[index].push_back(it);
				D.partId[index] = objId;
				D.empty = false;
			}
		}
	}

	// =========================================================================
	// cache
	// =========================================================================
	u64 SlotKey(int sx, int sz)
	{
		return (u64(u32(sx)) << 32) | u64(u32(sz));
	}

	void RebuildPending(int csx, int csz)
	{
		s_pending.clear();
		xr_vector<std::pair<float, u64> > tmp;
		int r = int(s_dmSize);
		for (int z = -r; z <= r; z++)
		{
			for (int x = -r; x <= r; x++)
			{
				u64 key = SlotKey(csx + x, csz + z);
				if (s_cache.find(key) != s_cache.end())
					continue;
				tmp.push_back(std::make_pair(float(x * x + z * z), key));
			}
		}
		std::sort(tmp.begin(), tmp.end());
		for (auto it = tmp.rbegin(); it != tmp.rend(); ++it)
			s_pending.push_back(it->second);	// nearest ends up at back()
	}

	void Update(int csx, int csz)
	{
		if (csx != s_camSX || csz != s_camSZ)
		{
			s_camSX = csx;
			s_camSZ = csz;
			RebuildPending(csx, csz);
		}

		int budget = kMaxDecompressPerFrame;
		while (budget-- > 0 && !s_pending.empty())
		{
			u64 key = s_pending.back();
			s_pending.pop_back();
			if (s_cache.find(key) != s_cache.end())
				continue;
			Decompressed& D = s_cache[key];
			DecompressSlot(int(key >> 32), int(key & 0xffffffff), D);
		}

		int limit = int(s_dmSize) + 2;
		for (auto it = s_cache.begin(); it != s_cache.end(); )
		{
			int sx = int(it->first >> 32);
			int sz = int(it->first & 0xffffffff);
			if (_abs(sx - csx) > limit || _abs(sz - csz) > limit)
				it = s_cache.erase(it);
			else
				++it;
		}
	}

	// =========================================================================
	// bgfx resources
	// =========================================================================
	bgfx_texture_handle_t TextureFor(const xr_string& name)
	{
		for (auto& e : s_textures)
			if (e.name == name)
				return e.handle;

		bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;
		unsigned int w = 0, h = 0;
		if (bgfxLoadWorldTexture(name.c_str(), tex, w, h) && bgfxIsValid(tex))
			LogInfo("[BGFX] Details tex '%s' %ux%u", name.c_str(), w, h);
		else
		{
			LogInfo("[BGFX] Details tex MISS '%s'", name.c_str());
			tex = BGFX_INVALID_HANDLE;
		}

		TexEntry e;
		e.name = name;
		e.handle = tex;
		s_textures.push_back(e);
		return tex;
	}

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
			bgfx_vertex_layout_add(&s_layoutDesc, BGFX_ATTRIB_TEXCOORD1, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
			bgfx_vertex_layout_end(&s_layoutDesc);
			s_layout = bgfx_create_vertex_layout(&s_layoutDesc);
			s_layoutReady = true;
		}

		std::vector<u8> vsBlob, psBlob;
		if (!bgfxShaderCompileFile("grass_vs.sc", 'v', vsBlob) ||
		    !bgfxShaderCompileFile("grass_ps.sc", 'f', psBlob))
		{
			LogError("[BGFX] Grass program build failed");
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
			LogError("[BGFX] Grass program build failed");
			return false;
		}

		s_sampler      = bgfx_create_uniform("u_texture", BGFX_UNIFORM_TYPE_SAMPLER, 1);
		s_alphaCtrl    = bgfx_create_uniform("u_grassAlpha", BGFX_UNIFORM_TYPE_VEC4, 1);
		s_grassParams  = bgfx_create_uniform("u_grassParams", BGFX_UNIFORM_TYPE_VEC4, 1);
		s_grassInt     = bgfx_create_uniform("u_grassInt", BGFX_UNIFORM_TYPE_VEC4, 1);
		s_bendersPos   = bgfx_create_uniform("benders_pos", BGFX_UNIFORM_TYPE_VEC4, kMaxBenders * 2);
		s_bendersSetup = bgfx_create_uniform("benders_setup", BGFX_UNIFORM_TYPE_VEC4, 1);
		LogInfo("[BGFX] Grass program created: %u", s_prog.idx);
		return true;
	}

	// =========================================================================
	// geometry expansion + submit
	// =========================================================================
	struct OutVertex
	{
		float	x, y, z;
		u32	color;
		float	u, v;
		float	height;
		float	pad;
	};

	u32 BakeColor(float c_hemi, float c_sun)
	{
		float l = 0.25f + 0.75f * (c_hemi * 0.55f + c_sun * 0.45f);
		clamp(l, 0.f, 1.f);
		u32 b = (u32)(l * 255.f + 0.5f);
		return 0xFF000000u | (b << 16) | (b << 8) | b;
	}

	void SubmitChunk(DetailModel& dobj, SlotItem** items, u32 count, bgfx_texture_handle_t tex)
	{
		const u32 nv = dobj.number_vertices;
		const u32 ni = dobj.number_indices;
		if (!nv || !ni)
			return;

		u32 maxBatch = kMaxVertsPerSubmit / nv;
		u32 byIdx = kMaxIndicesPerSubmit / ni;
		if (byIdx < maxBatch)
			maxBatch = byIdx;
		if (!maxBatch)
			return;

		u32 done = 0;
		while (done < count)
		{
			u32 batch = count - done;
			if (batch > maxBatch)
				batch = maxBatch;

			u32 nV = batch * nv;
			u32 nI = batch * ni;

			bgfx_transient_vertex_buffer_t tvb;
			bgfx_transient_index_buffer_t tib;
			if (!bgfx_alloc_transient_buffers(&tvb, &s_layoutDesc, nV, &tib, nI, false))
				return;

			OutVertex* vdst = (OutVertex*)tvb.data;
			u8* idst = (u8*)tib.data;

			for (u32 inst = 0; inst < batch; inst++)
			{
				SlotItem& it = *items[done + inst];
				const float scale = it.scale;
				const Fmatrix& M = it.M;
				const u32 color = BakeColor(it.c_hemi, it.c_sun);
				const u16 vbase = u16(inst * nv);

				for (u32 v = 0; v < nv; v++)
				{
					const DetailVertexIn& sv = dobj.vertices[v];
					const float px = sv.P.x * scale;
					const float py = sv.P.y * scale;
					const float pz = sv.P.z * scale;
					const float h = M._21 * px + M._22 * py + M._23 * pz;
					vdst->x = M._11 * px + M._21 * py + M._31 * pz + M._41;
					vdst->y = M._12 * px + M._22 * py + M._32 * pz + M._42;
					vdst->z = M._13 * px + M._23 * py + M._33 * pz + M._43;
					vdst->color = color;
					vdst->u = sv.u;
					vdst->v = sv.v;
					vdst->height = h;
					vdst->pad = 0.f;
					vdst++;
				}

				for (u32 i = 0; i < ni; i++)
					((u16*)idst)[i] = u16(vbase + dobj.indices[i]);
				idst += ni * 2;
			}

			const float gp[4] = { Device.fTimeGlobal, 0.12f, 0.6f, 0.4f };
			const float ac[4] = { 0.5f, 1.f, 0.f, 0.f };
			if (bgfxIsValid(s_grassParams))
				bgfx_set_uniform(s_grassParams, gp, 1);
			if (bgfxIsValid(s_alphaCtrl))
				bgfx_set_uniform(s_alphaCtrl, ac, 1);

			static Fvector4 benders[kMaxBenders * 2];
			for (int bi = 0; bi < kMaxBenders * 2; bi++)
				benders[bi].set(0.f, 0.f, 0.f, 0.f);
			int qty = 0;
			if (g_pGamePersistent && ps_ssfx_grass_interactive.y > 0)
			{
				qty = _min(kMaxBenders, (int)(ps_ssfx_grass_interactive.y + 1));
				IGame_Persistent::grass_data& G = g_pGamePersistent->grass_shader_data;
				if (ps_ssfx_grass_interactive.x > 0)
					benders[0].set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1.f);
				benders[kMaxBenders].set(0.f, -99.f, 0.f, 1.f);
				for (int b = 1; b < qty; b++)
				{
					benders[b].set(G.pos[b].x, G.pos[b].y, G.pos[b].z, G.radius_curr[b]);
					benders[b + kMaxBenders].set(G.dir[b].x, G.dir[b].y, G.dir[b].z, G.str[b]);
				}
			}
			if (bgfxIsValid(s_bendersPos))
				bgfx_set_uniform(s_bendersPos, benders, kMaxBenders * 2);
			if (bgfxIsValid(s_bendersSetup))
			{
				const float bs[4] = { ps_ssfx_int_grass_params_1.x, ps_ssfx_int_grass_params_1.y,
						ps_ssfx_int_grass_params_1.z, ps_ssfx_int_grass_params_1.w };
				bgfx_set_uniform(s_bendersSetup, bs, 1);
			}
			if (bgfxIsValid(s_grassInt))
			{
				const float gi[4] = { (float)qty, 0.f, 0.f, 0.f };
				bgfx_set_uniform(s_grassInt, gi, 1);
			}

			const uint64_t st = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
				| BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
			bgfx_set_state(st, 0);
			bgfx_set_transient_vertex_buffer(0, &tvb, 0, nV);
			bgfx_set_transient_index_buffer(&tib, 0, nI);
			if (bgfxIsValid(s_sampler))
				bgfx_set_texture(0, s_sampler, tex, UINT32_MAX);
			bgfx_submit(0, s_prog, 0, BGFX_DISCARD_ALL);

			done += batch;
		}
	}
} // namespace

// =============================================================================
// public API
// =============================================================================
void bgfxDetailsLoad()
{
	bgfxDetailsUnload();

	s_fs = FS.r_open("$level$", "level.details");
	if (!s_fs)
	{
		LogInfo("[BGFX] Details: level.details NOT FOUND");
		return;
	}

	s_fs->r_chunk_safe(0, &s_header, sizeof(s_header));
	if (s_header.version != DETAIL_VERSION)
	{
		LogInfo("[BGFX] Details: bad version %u", s_header.version);
		FS.r_close(s_fs);
		s_fs = nullptr;
		return;
	}

	IReader* mfs = s_fs->open_chunk(1);
	if (mfs)
	{
		for (u32 m_id = 0; m_id < s_header.object_count; m_id++)
		{
			IReader* S = mfs->open_chunk(m_id);
			if (!S)
				break;
			DetailModel* m = xr_new<DetailModel>();

			char fnS[256] = {}, fnT[256] = {};
			S->r_stringZ(fnS, sizeof(fnS));
			S->r_stringZ(fnT, sizeof(fnT));
			m->texture = fnT;

			m->flags.assign(S->r_u32());
			m->fMinScale = S->r_float();
			m->fMaxScale = S->r_float();
			m->number_vertices = S->r_u32();
			m->number_indices = S->r_u32();

			if (m->number_vertices && m->number_indices)
			{
				m->vertices = xr_alloc<DetailVertexIn>(m->number_vertices);
				S->r(m->vertices, m->number_vertices * sizeof(DetailVertexIn));
				m->indices = xr_alloc<u16>(m->number_indices);
				S->r(m->indices, m->number_indices * sizeof(u16));

				m->bv_bb.invalidate();
				for (u32 i = 0; i < m->number_vertices; i++)
					m->bv_bb.modify(m->vertices[i].P);
				m->bv_bb.getsphere(m->bv_sphere.P, m->bv_sphere.R);
			}
			s_objects.push_back(m);
			S->close();
		}
		mfs->close();
	}

	IReader* slots = s_fs->open_chunk(2);
	if (slots)
	{
		u32 bytes = (u32)slots->length();
		s_slots.resize(bytes / sizeof(DetailSlot));
		if (bytes && !s_slots.empty())
			slots->r(&s_slots[0], bytes);
		slots->close();
	}

	bwdithermap(2, s_dither);

	s_dmSize = u32(iFloor(float(s_radius) / 4.f) * 2);
	s_dmFade = float(2 * int(s_dmSize)) - 0.5f;

	s_loaded = true;
	LogInfo("[BGFX] Details: loaded objects=%u slots=%u size=%ux%u dm=%u",
		(u32)s_objects.size(), (u32)s_slots.size(), s_header.size_x, s_header.size_z, s_dmSize);
}

void bgfxDetailsUnload()
{
	for (DetailModel* m : s_objects)
		xr_delete(m);
	s_objects.clear();
	s_slots.clear();
	s_cache.clear();
	s_pending.clear();
	s_camSX = INT_MIN;
	s_camSZ = INT_MIN;
	if (s_fs)
	{
		FS.r_close(s_fs);
		s_fs = nullptr;
	}
	s_loaded = false;
}

void bgfxDetailsRender()
{
	if (!s_loaded || !s_fs || s_objects.empty())
		return;
	if (!psDeviceFlags.is(rsDetails))
		return;
	if (!EnsureProgram() || !bgfxIsValid(s_prog))
		return;

	const int csx = iFloor(Device.vCameraPosition.x / DETAIL_SLOT_SIZE + .5f);
	const int csz = iFloor(Device.vCameraPosition.z / DETAIL_SLOT_SIZE + .5f);
	Update(csx, csz);

	const float fadeLimit = s_dmFade * s_dmFade;
	const u32 nObjs = (u32)s_objects.size();
	if (nObjs > (u32)kMaxObjects)
		return;

	xr_vector<SlotItem*> gather[kMaxObjects];

	for (auto& kv : s_cache)
	{
		Decompressed& D = kv.second;
		if (D.empty)
			continue;
		if (Device.vCameraPosition.distance_to_sqr(D.center) > fadeLimit)
			continue;
		for (int part = 0; part < 4; part++)
		{
			u8 oid = D.partId[part];
			if (oid == DetailSlot::ID_Empty || oid >= nObjs)
				continue;
			for (SlotItem& it : D.items[part])
				gather[oid].push_back(&it);
		}
	}

	u32 drawn = 0;
	for (u32 o = 0; o < nObjs; o++)
	{
		xr_vector<SlotItem*>& list = gather[o];
		if (list.empty())
			continue;

		DetailModel& dobj = *s_objects[o];
		bgfx_texture_handle_t tex = TextureFor(dobj.texture);
		if (!bgfxIsValid(tex))
			tex = bgfxUIWhiteTextureGet();

		SubmitChunk(dobj, list.data(), (u32)list.size(), tex);
		drawn += (u32)list.size();
	}

	static u32 s_statFrames = 0;
	if (++s_statFrames >= 120)
	{
		s_statFrames = 0;
		LogInfo("[BGFX] Details: slots=%u items=%u", (u32)s_cache.size(), drawn);
	}
}
