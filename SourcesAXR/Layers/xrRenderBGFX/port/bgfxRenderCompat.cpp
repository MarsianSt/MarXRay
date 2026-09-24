#include "stdafx.h"
#pragma hdrstop

#include "bgfxRenderCompat.h"
#include "bgfxVisualTypes.h"
#include "bgfxVisualCompat.h"
#include "bgfxD3DX.h"
#include "ModelPool.h"
#include "FBasicVisual.h"
#include "FVisual.h"
#include "FProgressive.h"
#include "FTreeVisual.h"
#include "FLOD.h"
#include "FHierrarhyVisual.h"
#include "FSkinned.h"
#include "SkeletonCustom.h"
#include "ParticleEffect.h"
#include "ParticleGroup.h"
#include "PSLibrary.h"
#include "../../../xrEngine/fmesh.h"
#include "../../../xrEngine/xrLevel.h"

#include "../bgfx_capi.h"
#include "../bgfxWorldProgram.h"
#include "../bgfxUIShader.h"
#include "../bgfxUIProgram.h"

#include "../../../xrEngine/Render.h"
#include "../../../xrEngine/IGame_Level.h"
#include "../../../xrEngine/IGame_Persistent.h"
#include "../../../xrEngine/Environment.h"
#include "../../../xrEngine/xr_object.h"
#include "../../../xrEngine/device.h"
#include "../../../xrEngine/CustomHUD.h"
#include "bgfxModelBridge.h"

// ============================================================================
// Global instances
// ============================================================================
CRender RImplementation;

CRender::CRender() : phase(PHASE_NONE), pool(nullptr) {}
CRender::~CRender()
{
	if (pool)
		xr_delete(pool);
}

void bgfxEnsureModelPool()
{
	if (!RImplementation.pool)
		RImplementation.pool = xr_new<CModelPool>();
}

// Submits one particle visual. CParticleEffect::Render feeds its live PAPI
// particle array to bgfxParticles::SubmitPAPI (particle view, kView=6).
// CParticleGroup has no Render of its own (same as the reference), so its
// items' visuals are walked and each effect rendered individually.
void bgfxDrawParticleVisual(dxRender_Visual* v)
{
	if (!v)
		return;
	switch (v->Type)
	{
	case MT_PARTICLE_EFFECT:
		static_cast<PS::CParticleEffect*>(v)->Render(0.f);
		break;
	case MT_PARTICLE_GROUP:
	{
		PS::CParticleGroup* g = static_cast<PS::CParticleGroup*>(v);
		for (PS::CParticleGroup::SItemVecIt it = g->items.begin(); it != g->items.end(); ++it)
		{
			xr_vector<dxRender_Visual*> visuals;
			it->GetVisuals(visuals);
			for (dxRender_Visual* e : visuals)
				bgfxDrawParticleVisual(e);
		}
		break;
	}
	default:
		break;
	}
}

// ============================================================================
// CPU-side geometry store.
// The ported visuals reference buffers by a global u32 id resolved through
// RImplementation.getVB/getIB/getVB_Format. The geometry container is parsed
// from level.geom into real, reference-counted CPU buffers keyed by id.
// Before that container is loaded the ids still resolve to the empty stubs
// below (used by game models that reference level-geometry ids early).
// ============================================================================

namespace
{
	const u32 MAX_GEOM_IDS = 4096;
	ID3DVertexBuffer*   s_vb[MAX_GEOM_IDS] = {};
	ID3DIndexBuffer*    s_ib[MAX_GEOM_IDS] = {};

	// Real geometry store (filled from level.geom)
	bool                                    s_geomLoaded = false;
	xr_vector<xr_vector<D3DVERTEXELEMENT9>> s_dcl;       // declarators (main)
	xr_vector<ID3DVertexBuffer*>            s_vbList;    // vertex buffers (main)
	xr_vector<ID3DIndexBuffer*>             s_ibList;    // index buffers (main)
	xr_vector<xr_vector<D3DVERTEXELEMENT9>> s_fastDcl;   // fast geometry (.geomx)
	xr_vector<ID3DVertexBuffer*>            s_fastVbList;
	xr_vector<ID3DIndexBuffer*>             s_fastIbList;
	xr_vector<FSlideWindowItem>             s_swis;      // slide-window items

	ID3DVertexBuffer*& vbSlot(int ID) { return s_vb[ID % MAX_GEOM_IDS]; }
	ID3DIndexBuffer*&  ibSlot(int ID) { return s_ib[ID % MAX_GEOM_IDS]; }

	void ensureVB(int ID)
	{
		if (!vbSlot(ID))
			vbSlot(ID) = bgfxCreateVertexBufferEmpty(1, 4);   // 1 vertex, 4 bytes
	}
	void ensureIB(int ID)
	{
		if (!ibSlot(ID))
			ibSlot(ID) = bgfxCreateIndexBufferEmpty(1);
	}

	void clearGeometryStore()
	{
		for (ID3DVertexBuffer* b : s_vbList)     if (b) b->Release();
		for (ID3DVertexBuffer* b : s_fastVbList) if (b) b->Release();
		for (ID3DIndexBuffer*  b : s_ibList)     if (b) b->Release();
		for (ID3DIndexBuffer*  b : s_fastIbList) if (b) b->Release();
		s_vbList.clear();  s_fastVbList.clear();
		s_ibList.clear();  s_fastIbList.clear();
		s_dcl.clear();     s_fastDcl.clear();
		for (FSlideWindowItem& swi : s_swis) xr_free(swi.sw);
		s_swis.clear();
		s_geomLoaded = false;
	}
}

ID3DVertexBuffer* CRender::getVB(int ID, bool fast)
{
	if (fast)
	{
		if (s_geomLoaded && ID >= 0 && ID < (int)s_fastVbList.size()) return s_fastVbList[ID];
		if (s_geomLoaded && ID >= 0 && ID < (int)s_vbList.size())     return s_vbList[ID];
	}
	else if (s_geomLoaded && ID >= 0 && ID < (int)s_vbList.size())
		return s_vbList[ID];
	ensureVB(ID);
	return vbSlot(ID);
}

ID3DIndexBuffer* CRender::getIB(int ID, bool fast)
{
	if (fast)
	{
		if (s_geomLoaded && ID >= 0 && ID < (int)s_fastIbList.size()) return s_fastIbList[ID];
		if (s_geomLoaded && ID >= 0 && ID < (int)s_ibList.size())     return s_ibList[ID];
	}
	else if (s_geomLoaded && ID >= 0 && ID < (int)s_ibList.size())
		return s_ibList[ID];
	ensureIB(ID);
	return ibSlot(ID);
}

D3DVERTEXELEMENT9* CRender::getVB_Format(int ID, bool fast)
{
	const xr_vector<xr_vector<D3DVERTEXELEMENT9>>& list = fast ? s_fastDcl : s_dcl;
	if (s_geomLoaded && ID >= 0 && ID < (int)list.size() && !list[ID].empty())
		return const_cast<D3DVERTEXELEMENT9*>(&list[ID][0]);
	if (fast && s_geomLoaded && ID >= 0 && ID < (int)s_dcl.size() && !s_dcl[ID].empty())
		return const_cast<D3DVERTEXELEMENT9*>(&s_dcl[ID][0]);
	static D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
	decl[0] = D3DVERTEXELEMENT9{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0};
	decl[1] = D3DVERTEXELEMENT9{0xFF,0xFF,D3DDECLTYPE_UNUSED,0,0,0};
	return decl;
}

ref_shader CRender::getShader(int id)
{
	(void)id;
	return ref_shader();   // shader pipeline stubbed -> empty ref
}

// ============================================================================
// Model pool bridge
// ============================================================================
IRenderVisual* CRender::model_Create(LPCSTR name, IReader* data)
{
	bgfxEnsureModelPool();
	return RImplementation.pool ? RImplementation.pool->Create(name) : nullptr;
}

IRenderVisual* CRender::model_CreateParticles(LPCSTR name)
{
	bgfxEnsureModelPool();
	if (!RImplementation.pool)
		return nullptr;

	CPSLibrary& lib = bgfxPSLibrary();
	PS::CPEDef* SE = lib.FindPED(name);
	if (SE)
		return RImplementation.pool->CreatePE(SE);

	PS::CPGDef* SG = lib.FindPGD(name);
	R_ASSERT3(SG, "Particle effect or group doesn't exist", name);
	if (!SG)
		return nullptr;
	return RImplementation.pool->CreatePG(SG);
}

IRenderVisual* CRender::model_CreateChild(LPCSTR name, IReader* data)
{
	bgfxEnsureModelPool();
	return RImplementation.pool ? RImplementation.pool->CreateChild(name, data) : nullptr;
}

IRenderVisual* CRender::model_Duplicate(IRenderVisual* V)
{
	bgfxEnsureModelPool();
	return RImplementation.pool ? RImplementation.pool->Instance_Duplicate(static_cast<dxRender_Visual*>(V)) : V;
}

void CRender::model_Delete(IRenderVisual*& V, BOOL bDiscard)
{
	bgfxEnsureModelPool();
	if (RImplementation.pool)
	{
		dxRender_Visual* dv = static_cast<dxRender_Visual*>(V);
		RImplementation.pool->Delete(dv, bDiscard);
		V = dv;
	}
}

IRenderVisual* CRender::getVisual(int id)
{
	if (id < 0 || id >= (int)Visuals.size())
		return nullptr;
	return Visuals[id];
}

namespace
{
	void dumpHex(const char* tag, IReader* c, u32 nbytes)
	{
		u32 n = c->length() < nbytes ? c->length() : nbytes;
		char hex[128];
		const u8* p = (const u8*)c->pointer();
		u32 off = 0;
		while (off < n)
		{
			u32 chunk = n - off;
			if (chunk > 60) chunk = 60;
			for (u32 b = 0; b < chunk; b++)
				sprintf(hex + b * 2, "%02X", p[off + b]);
			hex[chunk * 2] = 0;
			LogInfo("%s+%04X: %s", tag, off, hex);
			off += chunk;
			if (off >= 512) break;
		}
	}

	void dumpChunkIterator(const char* tag, IReader* fs, int maxN)
	{
		u32 id = 0;
		IReader* prev = 0;
		int n = 0;
		while (n < maxN && (prev = fs->open_chunk_iterator(id, prev)) != 0)
		{
			if (n < 60)
				LogInfo("%s[%d] id=%u len=%u", tag, n, id, (u32)prev->length());
			++n;
		}
		LogInfo("%s total=%d", tag, n);
	}
}

void CRender::load_visuals(IReader* fs)
{
	// --- bgfxport: dump level packet layout so we can locate the geometry
	IReader* vis = fs->open_chunk(fsL_VISUALS);
	if (!vis)
	{
		LogInfo("VIS: fsL_VISUALS NOT FOUND");
		return;
	}
	dumpHex("VIS3", vis, 512);
	dumpChunkIterator("VIS3i", vis, 400);
	// --- end dump

	bgfxEnsureModelPool();

	IReader* chunk = 0;
	u32 index = 0;
	dxRender_Visual* V = 0;
	ogf_header H;

	while ((chunk = vis->open_chunk(index)) != 0)
	{
		chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
		if (index < 8)
			LogInfo("LVIS[%u] type=%u shader_id=%u", index, (u32)H.type, (u32)H.shader_id);
		if (index < 2)
			dumpChunkIterator("VISOGF", chunk, 40);
		V = pool->Instance_Create((u16)H.type);
		V->Load(0, chunk, 0);
		Visuals.push_back(V);
		LogInfo("LVIS visual[%u] type=%u", index, (u32)H.type);
		chunk->close();
		index++;
	}
	vis->close();
	LogInfo("VIS: visuals loaded=%u", (u32)Visuals.size());
}

// ============================================================================
// level.geom: fsL_VB / fsL_IB containers (mirror of the original R2 LoadBuffers)
// ============================================================================
void CRender::load_buffers(IReader* geomFile, bool fast)
{
	xr_vector<xr_vector<D3DVERTEXELEMENT9>>& _DCL = fast ? s_fastDcl  : s_dcl;
	xr_vector<ID3DVertexBuffer*>&            _VB  = fast ? s_fastVbList : s_vbList;
	xr_vector<ID3DIndexBuffer*>&             _IB  = fast ? s_fastIbList : s_ibList;

	// Vertex buffers
	IReader* fs = geomFile->open_chunk(fsL_VB);
	if (!fs)
	{
		LogInfo("GEOM: fsL_VB NOT FOUND");
		return;
	}
	u32 count = fs->r_u32();
	_DCL.resize(count);
	_VB.resize(count);
	for (u32 i = 0; i < count; i++)
	{
		// decl (probe in place - always terminated by D3DDECL_END)
		const D3DVERTEXELEMENT9* dclp = (const D3DVERTEXELEMENT9*)fs->pointer();
		u32 dcl_len = D3DXGetDeclLength(dclp) + 1;
		_DCL[i].resize(dcl_len);
		fs->r(&_DCL[i][0], dcl_len * sizeof(D3DVERTEXELEMENT9));

		u32 vCount = fs->r_u32();
		u32 vSize  = D3DXGetDeclVertexSize(&_DCL[i][0], 0);
		LogInfo("* [Loading VB] %d verts, %d Kb (stride %d) decl[%d]", vCount, (vCount*vSize)/1024, vSize, dcl_len);

		ID3DVertexBuffer* vb = bgfxCreateVertexBufferEmpty(vCount, vSize);
		if (vb->data.size())
			fs->r(&vb->data[0], (int)vb->data.size());
		_VB[i] = vb;
	}
	fs->close();

	// Index buffers
	fs = geomFile->open_chunk(fsL_IB);
	if (!fs)
	{
		LogInfo("GEOM: fsL_IB NOT FOUND");
		return;
	}
	count = fs->r_u32();
	_IB.resize(count);
	for (u32 i = 0; i < count; i++)
	{
		u32 iCount = fs->r_u32();
		LogInfo("* [Loading IB] %d indices, %d Kb", iCount, (iCount*2)/1024);

		ID3DIndexBuffer* ib = bgfxCreateIndexBufferEmpty(iCount);
		if (ib->data.size())
			fs->r(&ib->data[0], (int)ib->data.size());
		_IB[i] = ib;
	}
	fs->close();

	LogInfo("GEOM: %s VB=%u IB=%u", fast ? "fast" : "main", (u32)_VB.size(), (u32)_IB.size());
}

// ============================================================================
// level.geom: fsL_SWIS container (mirror of the original R2 LoadSWIs)
// ============================================================================
void CRender::load_swis(IReader* geomFile)
{
	IReader* fs = geomFile->open_chunk(fsL_SWIS);
	if (!fs)
	{
		LogInfo("GEOM: fsL_SWIS NOT FOUND");
		return;
	}
	u32 item_count = fs->r_u32();
	for (FSlideWindowItem& swi : s_swis) if (swi.sw) xr_free(swi.sw);
	s_swis.clear();
	s_swis.resize(item_count);
	for (u32 c = 0; c < item_count; c++)
	{
		FSlideWindowItem& swi = s_swis[c];
		swi.reserved[0] = fs->r_u32();
		swi.reserved[1] = fs->r_u32();
		swi.reserved[2] = fs->r_u32();
		swi.reserved[3] = fs->r_u32();
		swi.count      = fs->r_u32();
		swi.sw         = xr_alloc<FSlideWindow>(swi.count);
		fs->r(swi.sw, sizeof(FSlideWindow) * swi.count);
	}
	fs->close();
	LogInfo("GEOM: swis=%u", item_count);
}

FSlideWindowItem* CRender::getSWI(u32 ID)
{
	if (ID < (u32)s_swis.size())
		return &s_swis[ID];
	static FSlideWindowItem s_dummy;
	static FSlideWindow    s_window[1] = {{ 0, 1, 1 }};
	static bool            s_init = (s_dummy.sw = s_window, s_dummy.count = 1, true);
	(void)ID; (void)s_init;
	return &s_dummy;
}

// ============================================================================
// World-render pass.
// The level geometry (s_vbList/s_ibList) is uploaded once into bgfx static
// buffers; the camera already arrives on view 0 through SetCacheXform, so a
// draw call here is submit(view 0, worldProgram) with identity model matrix.
// First iteration: positions only, flat unlit color, depth test+write, no
// culling (avoids relying on winding until normals/culling are added).
// ============================================================================

namespace
{
	const uint64_t WORLD_STATE =
		  BGFX_STATE_WRITE_RGB
		| BGFX_STATE_WRITE_Z
		| BGFX_STATE_DEPTH_TEST_LESS
		| BGFX_STATE_MSAA;

	bgfx_vertex_layout_t              s_worldLayoutDesc = {};
	bgfx_vertex_layout_handle_t       s_worldLayout     = BGFX_INVALID_HANDLE;
	bgfx_vertex_layout_t              s_worldTerrainLayoutDesc = {};
	bgfx_vertex_layout_handle_t       s_worldTerrainLayout     = BGFX_INVALID_HANDLE;
	xr_vector<bgfx_vertex_buffer_handle_t> s_worldVbh;   // index-aligned with s_vbList
	xr_vector<bgfx_index_buffer_handle_t>  s_worldIbh;   // index-aligned with s_ibList
	xr_vector<u8>                        s_worldVbHasUv1; // 1 = buffer includes lightmap UV1
	bgfx_index_buffer_handle_t           s_worldLodIbh = BGFX_INVALID_HANDLE;
	int                                 s_worldUploaded = 0;
	int                                 g_worldDiagLogN = 0;
	bgfx_program_handle_t               s_worldProgram = BGFX_INVALID_HANDLE;
	bgfx_program_handle_t               s_worldDecalProgram = BGFX_INVALID_HANDLE;
	bgfx_program_handle_t               s_worldTerrainProgram = BGFX_INVALID_HANDLE;

	// Byte offset of the POSITION float3 in a vertex declaration, or -1.
	int worldPosOffset(const xr_vector<D3DVERTEXELEMENT9>& dcl)
	{
		for (const D3DVERTEXELEMENT9& e : dcl)
		{
			if (e.Stream == 0 && e.Usage == D3DDECLUSAGE_POSITION && e.Type == D3DDECLTYPE_FLOAT3)
				return e.Offset;
		}
		return -1;
	}

	// Byte offset of TEXCOORD0 in a vertex declaration, or -1. The level
	// sector geometry stores UVs packed: FLOAT2, SHORT2, SHORT4 or SHORT4N.
	int worldUvOffset(const xr_vector<D3DVERTEXELEMENT9>& dcl, int& outType)
	{
		for (const D3DVERTEXELEMENT9& e : dcl)
		{
			if (e.Stream == 0 && e.Usage == D3DDECLUSAGE_TEXCOORD && e.UsageIndex == 0)
			{
				if (e.Type != D3DDECLTYPE_FLOAT2 && e.Type != D3DDECLTYPE_FLOAT16_2
					&& e.Type != D3DDECLTYPE_SHORT2 && e.Type != D3DDECLTYPE_SHORT4
					&& e.Type != D3DDECLTYPE_SHORT4N && e.Type != D3DDECLTYPE_SHORT2N)
					continue;
				outType = e.Type;
				return e.Offset;
			}
		}
		return -1;
	}
	int worldUvOffset(const xr_vector<D3DVERTEXELEMENT9>& dcl)
	{
		int t = 0;
		return worldUvOffset(dcl, t);
	}

	// IEEE 754 half -> float (used to expand FLOAT16_2 lightmap UVs).
	float unpackHalf(u16 h)
	{
		const u32 sign = (u32)((h >> 15) & 1u) << 31u;
		const u32 exp  = (u32)((h >> 10) & 0x1Fu);
		u32       man  = (u32)(h & 0x3FFu);
		u32       bits;
		if (exp == 0)
		{
			if (man == 0)
				bits = sign;
			else
			{
				// subnormal half
				int e = -24;
				u32 m = man;
				while ((m & 0x400u) == 0)
				{
					m <<= 1;
					++e;
				}
				m &= 0x3FFu;
				bits = sign | ((u32)(e + 127) << 23u) | (m << 13u);
			}
		}
		else if (exp == 0x1F)
			bits = sign | 0x7F800000u | (man << 13u); // inf/nan
		else
			bits = sign | ((exp + (u32)(127 - 15)) << 23u) | (man << 13u);
		float f;
		memcpy(&f, &bits, sizeof(f));
		return f;
	}

	struct WorldDiag
	{
		int drawn     = 0;
		int total     = 0;
		int skipType  = 0;
		int skipVb    = 0;
		int skipIb    = 0;
		int skipVar   = 0;
		int upSkipVb  = 0;
		int upSkipIb  = 0;
		int byType[32] = { 0 };
	};

	static WorldDiag g_worldDiag;

	// Texture cache for the world pass: diffuse texture name -> bgfx handle.
	// Loaded once via bgfxLoadUITexture (existing DDS/TGA loader), cached by
	// name so repeated meshes share one GPU texture.
	struct WorldTexEntry { xr_string name; bgfx_texture_handle_t handle; };
	xr_vector<WorldTexEntry> g_worldTextures;
	bgfx_uniform_handle_t   g_worldSampler = BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t   g_worldAlphaCtrl = BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t   g_worldFogParams = BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t   g_worldFogColor = BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t   g_worldMaskSampler = BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t   g_worldDtSamplers[4] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
	bgfx_uniform_handle_t   g_worldDtScale = BGFX_INVALID_HANDLE;
	bool g_worldTexturesReady = false;

	// Level shader name table from fsL_SHADERS. index 0 is empty, entries are
	// names like "effects\glow/glow\glow_red2" (texture paths, mixed slashes).
	xr_vector<xr_string> g_levelShaders;
	bool s_worldDecalPass = false;

	// Children of flora impostors (MT_LOD / FLOD) are present in the level's
	// Visuals array as top-level entries too (referenced by OGF_CHILDREN_L),
	// but the reference submits them only while the impostor is close enough
	// (r__dsgraph_build.cpp, MT_LOD case). Collected once so the top-level walk
	// skips them and bgfxWorldDrawLod decides when they are drawn.
	xr_set<dxRender_Visual*> s_flodChildren;
	bool s_flodChildrenReady = false;

	enum WorldDecalKind
	{
		WDK_NONE = 0,
		WDK_ALPHA,
		WDK_MULTIPLY,
	};

	WorldDecalKind bgfxWorldDecalKind(const char* shaderName)
	{
		if (!shaderName || !shaderName[0])
			return WDK_NONE;
		if (strstr(shaderName, "wallmarkblend"))
			return WDK_ALPHA;
		if (strstr(shaderName, "wallmarkmult"))
			return WDK_MULTIPLY;
		return WDK_NONE;
	}

	const char* bgfxLevelShaderName(u16 shader_id)
	{
		if (shader_id == 0 || shader_id >= (u16)g_levelShaders.size())
			return nullptr;
		return g_levelShaders[shader_id].c_str();
	}

	xr_string bgfxLevelTextureName(const char* shaderName);
	xr_string bgfxLevelTextureNamePart(const xr_string& s);
	xr_string bgfxLevelShaderNameOnly(const char* shaderName);
	bool      bgfxWorldIsTerrainShader(const char* shaderName);
	bool      bgfxWorldTerrainDetail(const char* baseName, xr_string& detailName, float& detailScale);
	bgfx_texture_handle_t bgfxWorldTextureGet(const char* name);

	// Detail textures of the BmmD ("implicit detail") level materials, read from
	// the blender library (shaders.xr chunk 2) the same way the reference
	// renderer does it (CResourceManager::OnDeviceCreate + CBlender_BmmD::Load).
	// R2-R/G/B/A are the four detail textures selected by <base>_mask channels.
	struct WorldBmmD
	{
		xr_string det[4];
	};
	xr_map<xr_string, WorldBmmD> s_bmmdDetails;
	bool s_bmmdDetailsReady = false;

	u32 bgfxWorldPropSize(u32 type)
	{
		switch (type)
		{
		case 1: case 2: case 3: case 9: case 10: return 64; // string64 payloads
		case 4: return 12;                                  // xrP_Integer
		case 5: return 12;                                  // xrP_Float
		case 6: return 4;                                   // xrP_BOOL
		default: return 0;                                  // marker
		}
	}

	void bgfxWorldSkipProp(IReader* F)
	{
		const u32 type = F->r_u32();
		F->skip_stringZ();
		switch (type)
		{
		case 7: // xrP_TOKEN: IDselected + Count, Count * (ID + string64)
			F->r_u32();
			F->advance(int(F->r_u32() * (4 + 64)));
			break;
		case 8: // xrP_CLSID: CLASS_ID + Count, Count * CLASS_ID
			F->r_u64();
			F->advance(int(F->r_u32() * 8));
			break;
		default:
			F->advance(int(bgfxWorldPropSize(type)));
			break;
		}
	}

	void bgfxWorldReadPropName(IReader* F, xr_string& out)
	{
		out.clear();
		const u32 type = F->r_u32();
		F->skip_stringZ();
		if (bgfxWorldPropSize(type) != 64)
			return;
		char buf[65] = {};
		F->r(buf, 64);
		buf[64] = 0;
		out = buf;
	}

	void bgfxWorldLoadBlenders()
	{
		if (s_bmmdDetailsReady)
			return;
		s_bmmdDetailsReady = true;

		IReader* F = FS.r_open("$game_data$", "shaders.xr");
		if (!F)
		{
			LogInfo("WORLD blenders: shaders.xr not found");
			return;
		}
		IReader* lib = F->open_chunk(2);
		if (lib)
		{
			const u64 bmmdCls = 0x426D6D446F6C6420ull; // MK_CLSID('B','m','m','D','o','l','d',' ')
			u32 chunkId = 0;
			IReader* ch = nullptr;
			while ((ch = lib->open_chunk_iterator(chunkId, ch)) != nullptr)
			{
				const u64 cls = ch->r_u64();
				char cname[128] = {};
				ch->r(cname, 128);
				cname[127] = 0;
				ch->advance(32 + 4); // cComputer, cTime
				const u16 version = ch->r_u16();
				ch->advance(2);      // struct padding
				if (cls != bmmdCls || version < 3)
					continue;

				// IBlender::Load + CBlender_BmmD::Load property order
				bgfxWorldSkipProp(ch); // marker "General"
				bgfxWorldSkipProp(ch); // "Priority"
				bgfxWorldSkipProp(ch); // "Strict sorting"
				bgfxWorldSkipProp(ch); // marker "Base Texture"
				bgfxWorldSkipProp(ch); // "Name"
				bgfxWorldSkipProp(ch); // "Transform"
				bgfxWorldSkipProp(ch); // marker "Detail map"
				bgfxWorldSkipProp(ch); // "Name"      (oT2_Name)
				bgfxWorldSkipProp(ch); // "Transform" (oT2_xform)

				WorldBmmD d;
				bgfxWorldReadPropName(ch, d.det[0]); // R2-R
				bgfxWorldReadPropName(ch, d.det[1]); // R2-G
				bgfxWorldReadPropName(ch, d.det[2]); // R2-B
				bgfxWorldReadPropName(ch, d.det[3]); // R2-A
				s_bmmdDetails[cname] = d;
			}
			lib->close();
		}
		FS.r_close(F);
		LogInfo("WORLD blenders: BmmD materials=%u", (u32)s_bmmdDetails.size());
	}

	struct WorldTerrainLayers
	{
		bgfx_texture_handle_t mask;    // <base>_mask, RGBA channels select the 4 details
		bgfx_texture_handle_t det[4];  // detail textures for mask.r / .g / .b / .a
		float detailScale;             // tile scale shared by all 4 details
	};

	bool bgfxWorldLevelTerrain(u16 shader_id, WorldTerrainLayers& out);

	// Resolve the diffuse texture for a level visual by its fsL_SHADERS index,
	// converting the shader name to the db texture path and caching the handle.
	// Bits 31..: resolved flag in bit 31 of the index range; simplest: use a
	// parallel "resolved" vector so failed lookups are cached too.
	struct WorldLevelTex
	{
		bgfx_texture_handle_t handle;   // diffuse base texture
		bgfx_texture_handle_t mask;     // <base>_mask (terrain only), invalid if none
		bgfx_texture_handle_t det[4];   // 4 detail textures (terrain only)
		float detailScale;              // detail tile scale from .thm (0 = none)
		u8 resolved;
	};
	static xr_vector<WorldLevelTex> g_worldLevelTexCache;

	bgfx_texture_handle_t bgfxWorldLevelTexture(u16 shader_id)
	{
		if (shader_id == 0)
			return BGFX_INVALID_HANDLE;
		if (shader_id >= (u16)g_worldLevelTexCache.size())
		{
			g_worldLevelTexCache.resize(g_levelShaders.size());
			for (auto& e : g_worldLevelTexCache)
			{
				e.handle = BGFX_INVALID_HANDLE;
				e.mask = BGFX_INVALID_HANDLE;
				for (int i = 0; i < 4; ++i)
					e.det[i] = BGFX_INVALID_HANDLE;
				e.detailScale = 0.0f;
				e.resolved = 0;
			}
		}
		WorldLevelTex& e = g_worldLevelTexCache[shader_id];
		if (e.resolved)
			return e.handle;

		e.resolved = 1;
		e.handle = BGFX_INVALID_HANDLE;
		e.mask = BGFX_INVALID_HANDLE;
		for (int i = 0; i < 4; ++i)
			e.det[i] = BGFX_INVALID_HANDLE;
		e.detailScale = 0.0f;
		const char* sn = bgfxLevelShaderName(shader_id);
		if (!sn || !sn[0])
			return e.handle;

		xr_string tname = bgfxLevelTextureName(sn);
		unsigned int w = 0, h = 0;
		if (bgfxLoadWorldTexture(tname.c_str(), e.handle, w, h))
		{
			if (bgfxIsValid(e.handle))
				LogInfo("WORLD tex '%s' %ux%u", tname.c_str(), w, h);
		}
		else
		{
			LogInfo("WORLD tex MISS '%s' (%s)", tname.c_str(), sn);
			e.handle = BGFX_INVALID_HANDLE;
		}

		// Terrain blends the (uni-copy) base painting with 4 tiled detail
		// textures selected by <base>_mask (BLENDER: CBlender_BmmD, R2/R3
		// "s_mask" RGBA -> s_dt_r/g/b/a). The 4 detail names are the blender's
		// R2-R/G/B/A properties from shaders.xr; the shared tile scale is the
		// base texture's .thm detail scale (THM_CHUNK_DETAIL_EXT = 0x0815),
		// which is what the reference feeds into dt_params. No lightmap: it is
		// neither loaded nor used (matching R3's s_mask/s_lmap removal).
		if (bgfxWorldIsTerrainShader(sn))
		{
			bgfxWorldLoadBlenders();
			const xr_string shaderName = bgfxLevelShaderNameOnly(sn);
			const auto bi = s_bmmdDetails.find(shaderName);
			xr_string detName;
			float detailScale = 0.0f;
			if (bi != s_bmmdDetails.end() && bgfxWorldTerrainDetail(tname.c_str(), detName, detailScale))
			{
				e.detailScale = detailScale;
				xr_string maskName = tname;
				maskName += "_mask";
				e.mask = bgfxWorldTextureGet(maskName.c_str());
				for (int i = 0; i < 4; ++i)
				{
					const xr_string& dn = bi->second.det[i];
					if (!dn.empty() && dn[0] != '$')
						e.det[i] = bgfxWorldTextureGet(dn.c_str());
				}
				LogInfo("WORLD BmmD '%s': R='%s' G='%s' B='%s' A='%s' scale=%.3f",
					shaderName.c_str(), bi->second.det[0].c_str(), bi->second.det[1].c_str(),
					bi->second.det[2].c_str(), bi->second.det[3].c_str(), detailScale);
			}
			else
			{
				LogInfo("WORLD BmmD '%s': no blender/detail", shaderName.c_str());
			}
		}
		return e.handle;
	}

	// Mask + 4 detail textures + shared scale for a terrain shader id (cached
	// in the same entry resolved by bgfxWorldLevelTexture). Handles are
	// BGFX_INVALID_HANDLE / scale 0 when the terrain mix is absent.
	bool bgfxWorldLevelTerrain(u16 shader_id, WorldTerrainLayers& out)
	{
		out.mask = BGFX_INVALID_HANDLE;
		for (int i = 0; i < 4; ++i)
			out.det[i] = BGFX_INVALID_HANDLE;
		out.detailScale = 0.0f;
		if (shader_id == 0 || shader_id >= (u16)g_worldLevelTexCache.size())
			return false;
		if (!g_worldLevelTexCache[shader_id].resolved)
			bgfxWorldLevelTexture(shader_id);
		if (shader_id >= (u16)g_worldLevelTexCache.size())
			return false;
		const WorldLevelTex& e = g_worldLevelTexCache[shader_id];
		out.mask = e.mask;
		for (int i = 0; i < 4; ++i)
			out.det[i] = e.det[i];
		out.detailScale = e.detailScale;
		return bgfxIsValid(e.mask) && e.detailScale > 0.0f;
	}

	//
	// Keep the world texture-name cache small; diffusion entries are stored by
	// name, sector textures resolve through the level table.
	//
	// Strip a raw (pre-comma) shader path down to its LAST TWO path segments,
	// e.g. "levels\zaton_asfalt/terrain\terrain_zaton" -> "terrain\terrain_zaton".
	xr_string bgfxLevelTextureNamePart(const xr_string& s)
	{
		xr_vector<xr_string> segs;
		size_t start = 0;
		for (size_t i = 0; i <= s.size(); ++i)
		{
			if (i == s.size() || s[i] == '/' || s[i] == '\\')
			{
				if (i > start)
					segs.push_back(s.substr(start, i - start));
				start = i + 1;
			}
		}
		if (segs.size() <= 1)
			return s; // nothing to strip
		xr_string out = segs[segs.size() - 2];
		out += '\\';
		out += segs[segs.size() - 1];
		return out;
	}

	// fsL_SHADERS entries are shader/material names with mixed slashes, e.g.
	// "def_shaders\def_vertex/mtl\mtl_bochka_01" or
	// "levels\zaton_asfalt/terrain\terrain_zaton,terrain\terrain_zaton_lm".
	// Textures live in the db under the LAST TWO path segments, e.g. "mtl\mtl_bochka_01".
	// Pairs are split at ',' - only the diffuse (first) part is used.
	xr_string bgfxLevelTextureName(const char* shaderName)
	{
		xr_string s = shaderName ? shaderName : "";
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == ',')
			{
				s = s.substr(0, i);
				break;
			}
		}
		return bgfxLevelTextureNamePart(s);
	}

	// fsL_SHADERS entries are "<shader>/<textures>"; the blender is registered
	// under the part before '/' (r4_loader.cpp: delim = strchr(n_sh,'/')).
	xr_string bgfxLevelShaderNameOnly(const char* shaderName)
	{
		xr_string s = shaderName ? shaderName : "";
		const size_t p = s.find('/');
		if (p != xr_string::npos)
			s = s.substr(0, p);
		return s;
	}

	// Terrain materials are the fsL_SHADERS entries whose diffuse (first)
	// part carries a "terrain" path segment, e.g. ".../terrain\terrain_zaton,...".
	bool bgfxWorldIsTerrainShader(const char* shaderName)
	{
		if (!shaderName)
			return false;
		xr_string baseName = bgfxLevelTextureName(shaderName);
		for (size_t i = 0; i + 8 <= baseName.size(); ++i)
		{
			if (baseName.compare(i, 8, "terrain\\") == 0)
				return true;
		}
		return false;
	}

	// Parse the base texture's .thm for the terrain detail (name + scale):
	// THM_CHUNK_DETAIL_EXT = 0x0815 holds r_stringZ(detail_name) + r_float(scale).
	// Called with the base texture path, e.g. "terrain\terrain_zaton"; the .thm
	// lives next to the seed geographic folder ($game_textures$\<base>.thm).
	bool bgfxWorldTerrainDetail(const char* baseName, xr_string& detailName, float& detailScale)
	{
		detailName.clear();
		detailScale = 0.0f;
		if (!baseName || !baseName[0])
			return false;

		xr_string thmPath = baseName;
		thmPath += ".thm";
		IReader* F = FS.r_open("$game_textures$", thmPath.c_str());
		if (!F)
		{
			LogInfo("WORLD thm MISS '%s'", thmPath.c_str());
			return false;
		}
		bool ok = false;
		if (F->find_chunk(0x0815))
		{
			F->r_stringZ(detailName);
			detailScale = F->r_float();
			ok = !detailName.empty() && detailScale > 0.0f;
		}
		FS.r_close(F);
		LogInfo("WORLD thm '%s' -> detail '%s' scale=%.3f ok=%d",
			thmPath.c_str(), detailName.c_str(), detailScale, ok ? 1 : 0);
		return ok;
	}

	void bgfxWorldEnsureTextures()
	{
		if (g_worldTexturesReady)
			return;
		g_worldSampler = bgfx_create_uniform("u_texture", BGFX_UNIFORM_TYPE_SAMPLER, 1);
		g_worldAlphaCtrl = bgfx_create_uniform("u_alphaCtrl", BGFX_UNIFORM_TYPE_VEC4, 1);
		g_worldFogParams = bgfx_create_uniform("u_fogParams", BGFX_UNIFORM_TYPE_VEC4, 1);
		g_worldFogColor = bgfx_create_uniform("u_fogColor", BGFX_UNIFORM_TYPE_VEC4, 1);
		g_worldMaskSampler = bgfx_create_uniform("u_mask", BGFX_UNIFORM_TYPE_SAMPLER, 1);
		for (int i = 0; i < 4; ++i)
		{
			char name[16];
			xr_sprintf(name, sizeof(name), "u_dt%d", i);
			g_worldDtSamplers[i] = bgfx_create_uniform(name, BGFX_UNIFORM_TYPE_SAMPLER, 1);
		}
		g_worldDtScale = bgfx_create_uniform("u_dtScale", BGFX_UNIFORM_TYPE_VEC4, 1);
		g_worldTexturesReady = true;
	}

	// Vanilla X-Ray distance fog (screenspace_fog.h SSFX_CALC_FOG): view-space
	// distance feeds fog_params = (-n*r, n, f, r), r = 1/(f-n), and the shader
	// lerps towards fog_color by saturate(d*r - n*r). Both are pushed per submit
	// because BGFX_DISCARD_ALL resets bindings after every draw.
	void bgfxSetFogUniforms()
	{
		float params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float fogColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (g_pGamePersistent)
		{
			CEnvDescriptorMixer* env = g_pGamePersistent->Environment().CurrentEnv;
			if (env)
			{
				float n = env->fog_near;
				float f = env->fog_far;
				float r = 0.0f;
				if (f - n <= 0.001f)
				{
					n = 0.0f;
					f = 0.0f;
				}
				else
					r = 1.0f / (f - n);
				params[0] = -n * r;
				params[1] = n;
				params[2] = f;
				params[3] = r;
				fogColor[0] = env->fog_color.x;
				fogColor[1] = env->fog_color.y;
				fogColor[2] = env->fog_color.z;
				fogColor[3] = env->fog_density;
			}
		}
		if (bgfxIsValid(g_worldFogParams))
			bgfx_set_uniform(g_worldFogParams, params, 1);
		if (bgfxIsValid(g_worldFogColor))
			bgfx_set_uniform(g_worldFogColor, fogColor, 1);
	}

	// Returns the bgfx texture handle for a diffuse texture name (0xFFFF if
	// unknown/load-failed). Loaded and cached lazily.
	bgfx_texture_handle_t bgfxWorldTextureGet(const char* name)
	{
		if (!name || !name[0] || (0 == _strnicmp(name, "null", 4)))
			return BGFX_INVALID_HANDLE;

		for (auto& e : g_worldTextures)
			if (e.name == name)
				return e.handle;

		bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;
		unsigned int w = 0, h = 0;
		bool ok = bgfxLoadWorldTexture(name, tex, w, h);

		WorldTexEntry e;
		e.name = name;
		e.handle = tex;
		g_worldTextures.push_back(e);
		if (ok && bgfxIsValid(tex))
			LogInfo("WORLD tex '%s' %ux%u h=%u", name, w, h, tex.idx);
		else
			LogInfo("WORLD tex MISS '%s'", name);
		return e.handle;
	}

	void bgfxWorldUploadBuffers()
	{
		if (s_worldUploaded)
			return;

		bgfx_vertex_layout_begin(&s_worldLayoutDesc, BGFX_RENDERER_TYPE_DIRECT3D11);
		bgfx_vertex_layout_add(&s_worldLayoutDesc, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		bgfx_vertex_layout_add(&s_worldLayoutDesc, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		bgfx_vertex_layout_end(&s_worldLayoutDesc);
		s_worldLayout = bgfx_create_vertex_layout(&s_worldLayoutDesc);

		bgfx_vertex_layout_begin(&s_worldTerrainLayoutDesc, BGFX_RENDERER_TYPE_DIRECT3D11);
		bgfx_vertex_layout_add(&s_worldTerrainLayoutDesc, BGFX_ATTRIB_POSITION, 3, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		bgfx_vertex_layout_add(&s_worldTerrainLayoutDesc, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		bgfx_vertex_layout_add(&s_worldTerrainLayoutDesc, BGFX_ATTRIB_TEXCOORD1, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		bgfx_vertex_layout_end(&s_worldTerrainLayoutDesc);
		s_worldTerrainLayout = bgfx_create_vertex_layout(&s_worldTerrainLayoutDesc);

		s_worldVbh.assign(s_vbList.size(), bgfx_vertex_buffer_handle_t{ 0xFFFF });
		s_worldIbh.assign(s_ibList.size(), bgfx_index_buffer_handle_t{ 0xFFFF });
		s_worldVbHasUv1.assign(s_vbList.size(), 0);

		for (u32 i = 0; i < (u32)s_vbList.size(); ++i)
		{
			ID3DVertexBuffer* vb = s_vbList[i];
			if (!vb || vb->data.empty() || vb->vCount == 0 || vb->vStride == 0)
				continue;

			int posOff = (i < (u32)s_dcl.size()) ? worldPosOffset(s_dcl[i]) : -1;
			int uvType = 0;
			int uvOff = (i < (u32)s_dcl.size()) ? worldUvOffset(s_dcl[i], uvType) : -1;

			// Diag: dump the declaration elements for this vertex buffer.
			{
				char dstr[512];
				int dp = 0;
				if (i >= (u32)s_dcl.size())
					dp = sprintf_s(dstr, sizeof(dstr), " (no decl)");
				else
				{
					for (const D3DVERTEXELEMENT9& e : s_dcl[i])
						dp += sprintf_s(dstr + dp, sizeof(dstr) - dp, " [%u:%u:%u:%u]", e.Offset, e.Type, e.Usage, e.UsageIndex);
				}
				LogInfo("WORLD VB[%u] stride=%u pos=%d uv0=%d(%d)%s", i, vb->vStride, posOff, uvOff, uvType, dstr);
			}

			int uvOff1 = -1;
			int uvType1 = 0;
			if (i < (u32)s_dcl.size())
			{
				for (const D3DVERTEXELEMENT9& e : s_dcl[i])
				{
					if (e.Stream == 0 && e.Usage == D3DDECLUSAGE_TEXCOORD && e.UsageIndex == 1)
					{
						uvOff1 = e.Offset;
						uvType1 = e.Type;
						break;
					}
				}
			}

			int tOff = -1;
			int bOff = -1;
			if (i < (u32)s_dcl.size())
			{
				for (const D3DVERTEXELEMENT9& e : s_dcl[i])
				{
					if (e.Stream != 0 || e.Type != D3DDECLTYPE_D3DCOLOR)
						continue;
					if (e.Usage == D3DDECLUSAGE_TANGENT && e.UsageIndex == 0)
						tOff = e.Offset;
					else if (e.Usage == D3DDECLUSAGE_BINORMAL && e.UsageIndex == 0)
						bOff = e.Offset;
				}
			}
			LogInfo("WORLD VB[%u] uv1=%d(%d) hasUv1=%d", i, uvOff1, uvType1, (uvOff1 >= 0) ? 1 : 0);

			// Original decode (shaders\shared\common.h): base uv is ALWAYS
			// TEXCOORD0: unpack_tc_base(tc, T.w, B.w) = (tc + frac) * 32/32768
			// (= /1024); TEXCOORD1 is the lightmap uv (unpack_tc_lmap = /32768)
			// and must never be used as diffuse coordinates.

			if (posOff < 0)
			{
				++g_worldDiag.upSkipVb;
				continue;
			}

			const bool hasUv1GT = (uvOff1 >= 0);
			const bool uv1TypeOk = (uvType1 == D3DDECLTYPE_FLOAT2 || uvType1 == D3DDECLTYPE_SHORT2 || uvType1 == D3DDECLTYPE_FLOAT16_2);
			const bool hasUv1 = hasUv1GT && uv1TypeOk;
			LogInfo("WORLD VB[%u] uv1typeOk=%d", i, uv1TypeOk ? 1 : 0);
			const u32 vstride = hasUv1 ? 28 : 20;
			s_worldVbHasUv1[i] = hasUv1 ? 1 : 0;

			const u32 vcount = vb->vCount;
			const u32 stride = vb->vStride;
			const bgfx_memory_t* mem = bgfx_alloc(vcount * vstride);
			const u8* src = &vb->data[0];
			u8* dst = (u8*)mem->data;
			const bool uvFloat = (uvType == D3DDECLTYPE_FLOAT2);
			const bool uv1Float = (uvType1 == D3DDECLTYPE_FLOAT2);
			const bool uv1Half = (uvType1 == D3DDECLTYPE_FLOAT16_2);
			for (u32 v = 0; v < vcount; ++v)
			{
				memcpy(dst + v * vstride, src + v * stride + posOff, 12);
				if (uvOff >= 0 && uvFloat)
					memcpy(dst + v * vstride + 12, src + v * stride + uvOff, 8);
				else if (uvOff >= 0)
				{
					const s16* s = (const s16*)(src + v * stride + uvOff);
					float*      f = (float*)(dst + v * vstride + 12);
					float du = 0.0f;
					float dv = 0.0f;
					if (uvType == D3DDECLTYPE_SHORT2)
					{
						if (tOff >= 0)
							du = (float)src[v * stride + tOff + 3] * (1.0f / 255.0f);
						if (bOff >= 0)
							dv = (float)src[v * stride + bOff + 3] * (1.0f / 255.0f);
						f[0] = ((float)s[0] + du) * (32.0f / 32768.0f);
						f[1] = ((float)s[1] + dv) * (32.0f / 32768.0f);
					}
					else if (uvType == D3DDECLTYPE_SHORT4)
					{
						// v_tree (FTreeVisual): tc is int4 (u, v, frac, unused)
						// and the shader scales it by consts.x = 1/FTreeVisual_quant
						// (= 1/2048, tree textures tile 16x16 per mesh).
						f[0] = (float)s[0] * (1.0f / 2048.0f);
						f[1] = (float)s[1] * (1.0f / 2048.0f);
					}
					else
					{
						f[0] = (float)s[0] * (32.0f / 32768.0f);
						f[1] = (float)s[1] * (32.0f / 32768.0f);
					}
				}
				else
					memset(dst + v * vstride + 12, 0, 8);

				// Lightmap uv (TEXCOORD1): unpack_tc_lmap = /32768 for the
				// packed types, raw float for FLOAT2.
				if (!hasUv1)
					continue;
				if (uv1Float)
					memcpy(dst + v * vstride + 20, src + v * stride + uvOff1, 8);
				else if (uv1Half)
				{
					// FLOAT16_2: expand half floats to full float.
					const u16* s = (const u16*)(src + v * stride + uvOff1);
					float*      f = (float*)(dst + v * vstride + 20);
					f[0] = unpackHalf(s[0]);
					f[1] = unpackHalf(s[1]);
				}
				else
				{
					const s16* s = (const s16*)(src + v * stride + uvOff1);
					float*      f = (float*)(dst + v * vstride + 20);
					f[0] = (float)s[0] * (1.0f / 32768.0f);
					f[1] = (float)s[1] * (1.0f / 32768.0f);
				}
			}

			const bgfx_vertex_layout_t* dcl = hasUv1 ? &s_worldTerrainLayoutDesc : &s_worldLayoutDesc;
			s_worldVbh[i] = bgfx_create_vertex_buffer(mem, dcl, 0);
		}

		for (u32 i = 0; i < (u32)s_ibList.size(); ++i)
		{
			ID3DIndexBuffer* ib = s_ibList[i];
			if (!ib || ib->data.empty())
			{
				++g_worldDiag.upSkipIb;
				continue;
			}
			const bgfx_memory_t* mem = bgfx_alloc((u32)ib->data.size());
			memcpy(mem->data, &ib->data[0], ib->data.size());
			s_worldIbh[i] = bgfx_create_index_buffer(mem, 0);
		}

		// Shared quad index buffer for flora impostors (X-Ray QuadIB order:
		// 0,1,2, 3,2,1 per 4 vertices).
		{
			static const u16 quadIdx[6] = { 0, 1, 2, 3, 2, 1 };
			const bgfx_memory_t* mem = bgfx_alloc(sizeof(quadIdx));
			memcpy(mem->data, quadIdx, sizeof(quadIdx));
			s_worldLodIbh = bgfx_create_index_buffer(mem, 0);
		}

		s_worldUploaded = 1;
		LogInfo("WORLD: uploaded VB=%u IB=%u",
			(u32)s_worldVbh.size(), (u32)s_worldIbh.size());
	}

	void bgfxWorldDrawMesh(IRender_Mesh* mesh, u16 shaderId, WorldDiag& dg, bool isTree, const float* treeXform = nullptr)
	{
		if (!mesh || !mesh->p_rm_Vertices || !mesh->p_rm_Indices)
			return;
		if (!mesh->vCount || !mesh->iCount)
			return;

		int vi = -1, ii = -1;
		for (u32 i = 0; i < s_worldVbh.size(); ++i)
			if (s_vbList[i] == mesh->p_rm_Vertices && s_worldVbh[i].idx != 0xFFFF) { vi = (int)i; break; }
		for (u32 i = 0; i < s_worldIbh.size(); ++i)
			if (s_ibList[i] == mesh->p_rm_Indices && s_worldIbh[i].idx != 0xFFFF)  { ii = (int)i; break; }
		if (vi < 0)
		{
			++dg.skipVb;
			return;
		}
		if (ii < 0)
		{
			++dg.skipIb;
			return;
		}

		const char* sh = bgfxLevelShaderName(shaderId);
		const WorldDecalKind decalKind = bgfxWorldDecalKind(sh);
		const bool isDecal = (decalKind != WDK_NONE);
		if (isDecal != s_worldDecalPass)
			return;
		const bool isTerrain = !isDecal && bgfxWorldIsTerrainShader(sh);
		const bool vbHasUv1 = (vi < (int)s_worldVbHasUv1.size()) && (s_worldVbHasUv1[vi] != 0);
		if (isTree)
		{
			static bool s_treeDiag = false;
			if (!s_treeDiag)
			{
				s_treeDiag = true;
				char dstr[256] = {};
				u32 dp = (u32)sprintf_s(dstr, sizeof(dstr), "VB[%d] hasUv1=%d shader=%s", vi, vbHasUv1 ? 1 : 0, sh ? sh : "(null)");
				if (vi >= 0 && vi < (int)s_vbList.size() && s_vbList[vi] && s_vbList[vi]->vCount > 0)
				{
					const ID3DVertexBuffer* tvb = s_vbList[vi];
					const s16* tc = (const s16*)(&tvb->data[0] + 24);
					dp += (u32)sprintf_s(dstr + dp, sizeof(dstr) - dp, " tc=%d,%d,%d,%d uv=%f,%f",
						tc[0], tc[1], tc[2], tc[3], (float)tc[0] * (1.0f / 2048.0f), (float)tc[1] * (1.0f / 2048.0f));
				}
				LogInfo("WORLD TREE: %s", dstr);
			}
		}
		{
			static bool s_terrainDiag = false;
			if (isTerrain && !s_terrainDiag)
			{
				s_terrainDiag = true;
				char dstr[256] = {};
				u32 dp = (u32)sprintf_s(dstr, sizeof(dstr), "VB[%d] hasUv1=%d", vi, vbHasUv1 ? 1 : 0);
				if (vi >= 0 && vi < (int)s_vbList.size() && s_vbList[vi])
				{
					const ID3DVertexBuffer* tvb = s_vbList[vi];
					const int uo = 28;
					if (tvb->vCount > 0)
					{
						const u8* p0 = &tvb->data[0];
						const u8* p1 = (tvb->vCount > 1) ? &tvb->data[tvb->vStride] : p0;
						const s16* u00 = (const s16*)(p0 + 24);
						const s16* u10 = (const s16*)(p0 + uo);
						const s16* u01 = (const s16*)(p1 + 24);
						const s16* u11 = (const s16*)(p1 + uo);
						const u8* t0 = p0 + 16;
						const u8* b0 = p0 + 20;
						dp += (u32)sprintf_s(dstr + dp, sizeof(dstr) - dp, " uv0[0]=%d,%d uv1[0]=%d,%d uv0[1]=%d,%d uv1[1]=%d,%d",
							u00[0], u00[1], u10[0], u10[1], u01[0], u01[1], u11[0], u11[1]);
						dp += (u32)sprintf_s(dstr + dp, sizeof(dstr) - dp, " T=[%03d,%03d,%03d,%03d] B=[%03d,%03d,%03d,%03d]",
							t0[0], t0[1], t0[2], t0[3], b0[0], b0[1], b0[2], b0[3]);
					}
				}
				float dscale = 0.0f;
				WorldTerrainLayers tl;
				bgfxWorldLevelTerrain(shaderId, tl);
				dscale = tl.detailScale;
				dp += (u32)sprintf_s(dstr + dp, sizeof(dstr) - dp, " mask=%s dt0=%s dt1=%s dt2=%s dt3=%s scale=%.3f prog=%s",
					bgfxIsValid(tl.mask) ? "ok" : "bad",
					bgfxIsValid(tl.det[0]) ? "ok" : "bad",
					bgfxIsValid(tl.det[1]) ? "ok" : "bad",
					bgfxIsValid(tl.det[2]) ? "ok" : "bad",
					bgfxIsValid(tl.det[3]) ? "ok" : "bad",
					dscale,
					bgfxIsValid(s_worldTerrainProgram) ? "ok" : "bad");
				LogInfo("WORLD TERR: %s", dstr);
			}
		}
		// Buffers that carry a lightmap slot (TEXCOORD1 SHORT2/FLOAT2) were
		// repacked to a 28-byte stride; those without stay at 20 bytes. The
		// draw layout must match the upload repacking, NOT derive from the
		// shader kind (non-terrain world VBs also carry uv1).
		const bgfx_vertex_layout_handle_t layout = vbHasUv1 ? s_worldTerrainLayout : s_worldLayout;
		bgfx_set_vertex_buffer_with_layout(0, s_worldVbh[vi], mesh->vBase, mesh->vCount, layout);
		bgfx_set_index_buffer(s_worldIbh[ii], mesh->iBase, mesh->iCount);
		const bool hasValidTerrain = isTerrain && vbHasUv1 && bgfxIsValid(s_worldTerrainProgram);
		bgfx_texture_handle_t tex = BGFX_INVALID_HANDLE;if (g_worldTexturesReady && bgfxIsValid(g_worldSampler))
		{
			tex = bgfxWorldLevelTexture(shaderId);
			if (!bgfxIsValid(tex))
				tex = bgfxUIWhiteTextureGet();
		}
		if (bgfxIsValid(g_worldSampler))
			bgfx_set_texture(0, g_worldSampler, tex, UINT32_MAX);
		if (hasValidTerrain && bgfxIsValid(g_worldMaskSampler))
		{
			WorldTerrainLayers tl;
			const bool haveMix = bgfxWorldLevelTerrain(shaderId, tl);
			float dscale = haveMix ? tl.detailScale : 0.0f;
			bgfx_texture_handle_t mask = haveMix ? tl.mask : bgfxUIWhiteTextureGet();
			bgfx_set_texture(1, g_worldMaskSampler, mask, UINT32_MAX);
			for (int i = 0; i < 4; ++i)
			{
				bgfx_texture_handle_t det = bgfxIsValid(tl.det[i]) ? tl.det[i] : bgfxUIWhiteTextureGet();
				bgfx_set_texture(2 + i, g_worldDtSamplers[i], det, UINT32_MAX);
			}
			if (bgfxIsValid(g_worldDtScale))
			{
				float dtScale[4] = { dscale, dscale, 0.0f, 0.0f };
				bgfx_set_uniform(g_worldDtScale, dtScale, 1);
			}
		}
		const bool isAref = sh && strstr(sh, "def_aref");
		const bool isTrans = sh && strstr(sh, "def_trans");
		const bool alphaTest = isAref || isDecal || isTree;
		const float alphaRef = (isAref || isTree) ? 0.33f : 0.001f;
		float alphaCtrl[4] = { alphaRef, alphaTest ? 1.0f : 0.0f, 0.0f, 0.0f };
		if (bgfxIsValid(g_worldAlphaCtrl))
			bgfx_set_uniform(g_worldAlphaCtrl, alphaCtrl, 1);

		uint64_t st = WORLD_STATE;
		if (decalKind == WDK_ALPHA)
		{
			st = BGFX_STATE_WRITE_RGB
				| BGFX_STATE_DEPTH_TEST_LEQUAL
				| BGFX_STATE_BLEND_ALPHA
				| BGFX_STATE_MSAA;
		}
		else if (decalKind == WDK_MULTIPLY)
		{
			st = BGFX_STATE_WRITE_RGB
				| BGFX_STATE_DEPTH_TEST_LEQUAL
				| BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_SRC_COLOR)
				| BGFX_STATE_MSAA;
		}
		else if (isTrans)
		{
			st = BGFX_STATE_WRITE_RGB
				| BGFX_STATE_DEPTH_TEST_LESS
				| BGFX_STATE_BLEND_ALPHA
				| BGFX_STATE_MSAA;
		}
		bgfx_set_state(st, 0);
		bgfx_program_handle_t prog = s_worldProgram;
		if (isDecal && bgfxIsValid(s_worldDecalProgram))
			prog = s_worldDecalProgram;
		else if (hasValidTerrain)
			prog = s_worldTerrainProgram;
		if (treeXform)
			bgfx_set_transform(treeXform, 1);
		bgfxSetFogUniforms();
		bgfx_submit(0, prog, 0, BGFX_DISCARD_ALL);
		if (treeXform)
		{
			static const float s_identityXform[16] =
			{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f,
			};
			bgfx_set_transform(s_identityXform, 1);
		}
		++dg.drawn;
	}

	// LOD selection thresholds for flora impostors, computed like the reference
	// (xrRender_R2/r2_R_calculate.cpp): g_fSCREEN = W*H*(90/FOV)^2*(EPS_S+lod),
	// r_ssaLOD_A = (ps_r2_ssaLOD_A/3)^2 / g_fSCREEN (and likewise for B/DISCARD).
	// ssa = (sphere.R / distSQ) * lod_factor (r__dsgraph_build.cpp MT_LOD case).
	float s_lodSsaA       = 0.0002f;
	float s_lodSsaB       = 0.0001f;
	float s_lodSsaDiscard = 0.000005f;
	u32   s_lodSsaW       = 0;
	u32   s_lodSsaH       = 0;
	float s_lodSsaFov     = 0.0f;

	void bgfxWorldUpdateLodThresholds()
	{
		const u32 w = Device.dwWidth;
		const u32 h = Device.dwHeight;
		const float fov = Device.fFOV;
		if (w == s_lodSsaW && h == s_lodSsaH && fov == s_lodSsaFov)
			return;
		s_lodSsaW = w;
		s_lodSsaH = h;
		s_lodSsaFov = fov;
		if (w == 0 || h == 0 || fov < 1.0f)
			return;

		const float geometryLod = 0.75f; // ps_r__LOD default (r__geometry_lod)
		const float fovFactor = (90.0f / fov) * (90.0f / fov);
		const float fscreen = float(w) * float(h) * fovFactor * (EPS_S + geometryLod);
		if (fscreen <= 1.0f)
			return;

		const float lodA = 64.0f;   // ps_r2_ssaLOD_A
		const float lodB = 48.0f;   // ps_r2_ssaLOD_B
		const float discard = 3.5f; // ps_r__ssaDISCARD
		s_lodSsaA = (lodA / 3.0f) * (lodA / 3.0f) / fscreen;
		s_lodSsaB = (lodB / 3.0f) * (lodB / 3.0f) / fscreen;
		s_lodSsaDiscard = discard * discard / fscreen;
		LogInfo("WORLD LOD thresholds: A=%.7f B=%.7f discard=%.7f (screen %ux%u fov %.1f lod %.2f)",
			s_lodSsaA, s_lodSsaB, s_lodSsaDiscard, w, h, fov, geometryLod);
	}

	void bgfxWorldDrawVisual(dxRender_Visual* v, WorldDiag& dg);

	// Level flora (MT_LOD / FLOD) renders as 8-facet impostors: pick the facet
	// whose normal faces the camera, emit its 4 corners shifted half a radius
	// towards the viewer, and draw with the regular world program. Reference:
	// r__dsgraph_render_lods.cpp (single-facet approximation, no fade blend).
	void bgfxWorldDrawLod(FLOD* lod, WorldDiag& dg)
	{
		if (!lod)
			return;
		if (s_worldDecalPass)
			return;

		const u16 shaderId = lod->shader_id;
		const char* sh = bgfxLevelShaderName(shaderId);

		Fvector Ldir;
		Ldir.sub(lod->vis.sphere.P, Device.vCameraPosition);
		const float len = Ldir.magnitude();
		if (len > 0.0001f)
			Ldir.div(len);
		else
			Ldir.set(0.0f, 0.0f, 1.0f);

		bgfxWorldUpdateLodThresholds();

		const float distSQ = Device.vCameraPosition.distance_to_sqr(lod->vis.sphere.P) + EPS;
		const float ssa = (lod->vis.sphere.R / distSQ) * lod->lod_factor;
		const bool hasChildren = !lod->children.empty();

		if (ssa < s_lodSsaDiscard)
			return;

		// Reference (r__dsgraph_build.cpp MT_LOD): children are submitted when
		// ssa > r_ssaLOD_B and the impostor only while ssa < r_ssaLOD_A. The
		// [B, A] overlap is a fade in the reference; here the impostor is drawn
		// only below A so it cannot cover the detail geometry.
		if (hasChildren && ssa >= s_lodSsaA)
		{
			for (dxRender_Visual* ch : lod->children)
				bgfxWorldDrawVisual(ch, dg);
			return;
		}

		u32 best = 0;
		float bestDot = Ldir.dotproduct(lod->facets[0].N);
		for (u32 s = 1; s < 8; ++s)
		{
			const float d = Ldir.dotproduct(lod->facets[s].N);
			if (d > bestDot)
			{
				bestDot = d;
				best = s;
			}
		}

		Fvector shift;
		shift.mul(Ldir, -0.5f * lod->vis.sphere.R);

		bgfx_transient_vertex_buffer_t tvb;
		bgfx_alloc_transient_vertex_buffer(&tvb, 4, &s_worldLayoutDesc);

		static const int vid[4] = { 3, 0, 2, 1 };
		const FLOD::_face& F = lod->facets[best];
		for (int i = 0; i < 4; ++i)
		{
			const FLOD::_vertex& sv = F.v[vid[i]];
			float* dst = (float*)((u8*)tvb.data + i * tvb.stride);
			dst[0] = sv.v.x + shift.x;
			dst[1] = sv.v.y + shift.y;
			dst[2] = sv.v.z + shift.z;
			dst[3] = sv.t.x;
			dst[4] = sv.t.y;
		}

		{
			static bool s_lodDiag = false;
			if (!s_lodDiag)
			{
				s_lodDiag = true;
				LogInfo("WORLD LOD: shader=%s R=%.3f children=%u ssa=%.7f dist=%.2f A=%.7f discard=%.7f best=%u",
					sh ? sh : "(null)", lod->vis.sphere.R, (u32)lod->children.size(),
					ssa, len, s_lodSsaA, s_lodSsaDiscard, best);
			}
		}

		if (g_worldTexturesReady && bgfxIsValid(g_worldSampler))
		{
			bgfx_texture_handle_t tex = bgfxWorldLevelTexture(shaderId);
			if (!bgfxIsValid(tex))
				tex = bgfxUIWhiteTextureGet();
			bgfx_set_texture(0, g_worldSampler, tex, UINT32_MAX);
		}
		if (bgfxIsValid(g_worldAlphaCtrl))
		{
			float alphaCtrl[4] = { 0.33f, 1.0f, 0.0f, 0.0f };
			bgfx_set_uniform(g_worldAlphaCtrl, alphaCtrl, 1);
		}

		bgfx_set_state(WORLD_STATE, 0);
		bgfx_set_transient_vertex_buffer(0, &tvb, 0, 4);
		bgfx_set_index_buffer(s_worldLodIbh, 0, 6);
		bgfxSetFogUniforms();
		bgfx_submit(0, s_worldProgram, 0, BGFX_DISCARD_ALL);
		++dg.drawn;
	}

	// Frame stamp used to draw each visual at most once per pass: the level's
	// Visuals array holds hierarchy children as top-level entries too (they are
	// referenced by OGF_CHILDREN_L), so a plain walk + recursion draws them twice.
	static u32 s_worldFrameMarker = 1;

	void bgfxWorldDrawVisual(dxRender_Visual* v, WorldDiag& dg)
	{
		if (!v)
			return;
		if (v->vis.marker == s_worldFrameMarker)
			return;
		v->vis.marker = s_worldFrameMarker;
		if (v->Type < 32)
			++dg.byType[v->Type];
		++dg.total;
		switch (v->Type)
		{
		case MT_NORMAL:
			bgfxWorldDrawMesh(static_cast<Fvisual*>(v), v->shader_id, dg, false);
			break;
		case MT_LOD:
			bgfxWorldDrawLod(static_cast<FLOD*>(v), dg);
			break;
		case MT_PROGRESSIVE:
		{
			FProgressive* pm = static_cast<FProgressive*>(v);
			FSlideWindow sw;
			if (pm->GetCurrentSlideWindow(sw))
			{
				const u32 oldVBase  = pm->vBase;
				const u32 oldVCount = pm->vCount;
				const u32 oldIBase  = pm->iBase;
				const u32 oldICount = pm->iCount;

				pm->vBase  = oldVBase;
				pm->vCount = sw.num_verts;
				pm->iBase  = oldIBase + sw.offset;
				pm->iCount = u32(sw.num_tris) * 3u;
				bgfxWorldDrawMesh(pm, v->shader_id, dg, false);

				pm->vBase  = oldVBase;
				pm->vCount = oldVCount;
				pm->iBase  = oldIBase;
				pm->iCount = oldICount;
			}
			else
			{
				bgfxWorldDrawMesh(pm, v->shader_id, dg, false);
			}
			break;
		}
		case MT_TREE_ST:
		{
			FTreeVisual* tv = static_cast<FTreeVisual*>(v);
			bgfxWorldDrawMesh(tv, v->shader_id, dg, true, tv->GetTreeXform());
			break;
		}
		case MT_TREE_PM:
		{
			FTreeVisual_PM* tp = static_cast<FTreeVisual_PM*>(v);
			FSlideWindow sw;
			if (tp->GetCurrentSlideWindow(sw))
			{
				const u32 oldVBase  = tp->vBase;
				const u32 oldVCount = tp->vCount;
				const u32 oldIBase  = tp->iBase;
				const u32 oldICount = tp->iCount;

				tp->vCount = sw.num_verts;
				tp->iBase  = oldIBase + sw.offset;
				tp->iCount = u32(sw.num_tris) * 3u;
				bgfxWorldDrawMesh(tp, v->shader_id, dg, true, tp->GetTreeXform());

				tp->vBase  = oldVBase;
				tp->vCount = oldVCount;
				tp->iBase  = oldIBase;
				tp->iCount = oldICount;
			}
			else
			{
				bgfxWorldDrawMesh(tp, v->shader_id, dg, true, tp->GetTreeXform());
			}
			break;
		}
		case MT_HIERRARHY:
		{
			FHierrarhyVisual* hv = static_cast<FHierrarhyVisual*>(v);
			for (dxRender_Visual* ch : hv->children)
				bgfxWorldDrawVisual(ch, dg);
			break;
		}
		case MT_PARTICLE_EFFECT:
		case MT_PARTICLE_GROUP:
			// Particles submit into the sky view (kView=2). Draw them only in
			// the base pass: the decal pass re-walks the visuals with a fresh
			// frame marker and would otherwise submit them twice.
			if (!s_worldDecalPass)
			{
				bgfxDrawParticleVisual(v);
				++dg.drawn;
			}
			break;
		default:
			++dg.skipType;
			break;
		}
	}
}

// ============================================================================
// C bridge used by the (non-port) bgfxRenderInterface
// ============================================================================
extern ENGINE_API float psHUD_FOV;

extern "C"
{
	void* bgfxModelCreate(const char* name)
	{
		bgfxEnsureModelPool();
		return RImplementation.model_Create(name);
	}
	void* bgfxModelCreateChild(const char* name, IReader* data)
	{
		bgfxEnsureModelPool();
		return RImplementation.model_CreateChild(name, data);
	}
	void* bgfxModelDuplicate(void* V)
	{
		bgfxEnsureModelPool();
		return RImplementation.model_Duplicate(static_cast<IRenderVisual*>(V));
	}
	void bgfxModelDelete(void** V, int bDiscard)
	{
		IRenderVisual* v = static_cast<IRenderVisual*>(*V);
		RImplementation.model_Delete(v, bDiscard);
		*V = v;
	}
	void* bgfxGetVisual(int id)
	{
		bgfxEnsureModelPool();
		return RImplementation.getVisual(id);
	}
	void bgfxLoadVisuals(IReader* fs)
	{
		bgfxEnsureModelPool();

		g_levelShaders.clear();
		IReader* r = fs->open_chunk(fsL_SHADERS);
		LogInfo("SHADERS chunk=%d len=%u", fsL_SHADERS, r ? (u32)r->length() : 0);
		if (r)
		{
			u32 sz = r->r_u32();
			g_levelShaders.reserve(sz);
			char sbuf[256];
			for (u32 i = 0; i < sz && !r->eof(); ++i)
			{
				memset(sbuf, 0, sizeof(sbuf));
				r->r_stringZ(sbuf, sizeof(sbuf));
				g_levelShaders.push_back(sbuf);
			}
			LogInfo("SHDR parsed=%u", (u32)g_levelShaders.size());
			r->close();
		}

		// Preload level textures here (during level load, outside the first
		// render frame). Loading ~400 DDS textures synchronously inside the
		// first frame used to stall it for tens of seconds.
		u32 preloadOk = 0, preloadMiss = 0;
		for (u32 si = 1; si < (u32)g_levelShaders.size(); ++si)
		{
			if (bgfxIsValid(bgfxWorldLevelTexture((u16)si)))
				++preloadOk;
			else
				++preloadMiss;
		}
		LogInfo("SHDR preload ok=%u miss=%u", preloadOk, preloadMiss);

		RImplementation.load_visuals(fs);
	}
	void bgfxLoadGeometry()
	{
		// parse shared geometry (VB/IB/SWI) from level.geom; must run BEFORE
		// load_visuals so visuals resolve real buffer ids.
		IReader* g = FS.r_open("$level$", "level.geom");
		if (!g)
		{
			LogInfo("GEOM: level.geom NOT FOUND");
			return;
		}
		clearGeometryStore();
		LogInfo("GEOM: total=%u bytes", (u32)g->length());
		RImplementation.load_buffers(g, false);
		RImplementation.load_swis(g);
		FS.r_close(g);
		s_geomLoaded = true;

		// upload the parsed geometry to bgfx once (lazy - program compiled at
		// first Render() call)
		bgfxWorldUploadBuffers();
	}
	void bgfxWorldCollectFlodChildren(dxRender_Visual* v, int depth)
	{
		if (!v || depth > 8)
			return;
		if (v->Type == MT_LOD)
		{
			FLOD* lod = static_cast<FLOD*>(v);
			for (dxRender_Visual* ch : lod->children)
			{
				s_flodChildren.insert(ch);
				bgfxWorldCollectFlodChildren(ch, depth + 1);
			}
		}
		else if (v->Type == MT_HIERRARHY)
		{
			FHierrarhyVisual* hv = static_cast<FHierrarhyVisual*>(v);
			for (dxRender_Visual* ch : hv->children)
				bgfxWorldCollectFlodChildren(ch, depth + 1);
		}
	}

	void bgfxRenderWorld()
	{
		if (!s_geomLoaded || !s_worldUploaded || RImplementation.Visuals.empty())
			return;

		static int s_buildTagLogged = 0;
		if (!s_buildTagLogged)
		{
			LogInfo("WORLD build tag: ui-clamp+scene-no-alpha-2026-09-09");
			s_buildTagLogged = 1;
		}

		bgfxWorldEnsureTextures();

		if (s_worldProgram.idx == 0xFFFF)
		{
			s_worldProgram = bgfxWorldProgramGet();
			if (s_worldProgram.idx == 0xFFFF)
				return;
		}
		if (s_worldDecalProgram.idx == 0xFFFF)
			s_worldDecalProgram = bgfxWorldDecalProgramGet();
		if (s_worldTerrainProgram.idx == 0xFFFF)
			s_worldTerrainProgram = bgfxWorldTerrainProgramGet();

		if (!s_flodChildrenReady)
		{
			s_flodChildrenReady = true;
			for (IRenderVisual* V0 : RImplementation.Visuals)
				bgfxWorldCollectFlodChildren(static_cast<dxRender_Visual*>(V0), 0);
			LogInfo("WORLD FLOD children=%u", (u32)s_flodChildren.size());
		}

		// pass 1: opaque / alpha-tested / transparent world geometry
		s_worldDecalPass = false;
		++s_worldFrameMarker;
		for (IRenderVisual* V0 : RImplementation.Visuals)
		{
			dxRender_Visual* v = static_cast<dxRender_Visual*>(V0);
			if (s_flodChildren.find(v) != s_flodChildren.end())
				continue;
			bgfxWorldDrawVisual(v, g_worldDiag);
		}

		// pass 2: projected decals (wallmark blend/multiply), on top of base surfaces
		s_worldDecalPass = true;
		++s_worldFrameMarker;
		for (IRenderVisual* V0 : RImplementation.Visuals)
		{
			dxRender_Visual* v = static_cast<dxRender_Visual*>(V0);
			if (s_flodChildren.find(v) != s_flodChildren.end())
				continue;
			bgfxWorldDrawVisual(v, g_worldDiag);
		}
		s_worldDecalPass = false;

		static int s_logged = 0;
		if (!s_logged)
		{
			char tb[512];
			int tp = 0;
			for (int t = 0; t < 32; ++t)
				if (g_worldDiag.byType[t])
					tp += sprintf_s(tb + tp, sizeof(tb) - tp, " %d=%d", t, g_worldDiag.byType[t]);
			LogInfo("WORLD: submitted=%d total=%d drawn=%d"
				" skipType=%d skipVb=%d skipIb=%d upSkipVb=%d upSkipIb=%d byType:%s",
				g_worldDiag.drawn, g_worldDiag.total, g_worldDiag.drawn,
				g_worldDiag.skipType, g_worldDiag.skipVb, g_worldDiag.skipIb,
				g_worldDiag.upSkipVb, g_worldDiag.upSkipIb, tb);
			s_logged = 1;
		}
		bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z, 0);
	}

	// ========================================================================
	// Reference dynamic pass (CRender::render_main, r4_R_render.cpp:147-201):
	// every renderable object asks the renderer for its visual. The port has no
	// portal/HOM traversal yet, so all level objects are asked; visuals are
	// accumulated by ::Render->add_Visual and drawn by bgfxRenderDynamic after
	// the world pass.
	// ========================================================================
	void bgfxRenderSceneObjects()
	{
		if (!g_pGameLevel)
			return;

		CObjectList& objects = g_pGameLevel->Objects;
		for (u32 i = 0; i < objects.o_count(); ++i)
		{
			CObject* O = objects.o_get_by_iterator(i);
			if (!O)
				continue;
			if (!(O->spatial.type & STYPE_RENDERABLE))
				continue;
			if (O->H_Parent())
				continue;
			IRenderable* R = O->dcast_Renderable();
			if (!R || !R->renderable.visual)
				continue;
			R->renderable_Render();
		}

		// Reference r__dsgraph_render.cpp:757-783 additionally walks
		// g_SpatialSpace for dynamic renderables. Particle objects
		// (CParticlesObject : CPS_Instance) are registered there, not in the
		// level's Objects list, so without this pass they never reach
		// add_Visual and are silently dropped from the frame. CObjects are
		// skipped here - the loop above already submitted them.
		if (g_SpatialSpace)
		{
			static xr_vector<ISpatial*> s_renderables;
			g_SpatialSpace->q_sphere(s_renderables, ISpatial_DB::O_ORDERED,
				STYPE_RENDERABLE, Device.vCameraPosition, 100000.f);
			for (u32 i = 0; i < s_renderables.size(); ++i)
			{
				ISpatial* spatial = s_renderables[i];
				if (!spatial || spatial->dcast_CObject())
					continue;
				IRenderable* R = spatial->dcast_Renderable();
				if (!R || !R->renderable.visual)
					continue;
				R->renderable_Render();
			}
		}
	}

	// ========================================================================
	// HUD pass (hands + weapon of the actor). Mirrors the reference sequence:
	// CHUDManager::Render_Last (HUDManager.cpp:227) collects the visuals through
	// g_player_hud with set_HUD(TRUE), then they are drawn with a dedicated
	// view/projection (hud_transform_helper, r__dsgraph_render.cpp:490). bgfx
	// resolves view transforms at frame end, so the HUD gets its own view with a
	// depth clear. The game UI is submitted into a later view (see
	// bgfxRenderDeviceRender::Begin) since CLevel::OnRender draws it after
	// Render->Render() and it belongs on top of the HUD.
	// ========================================================================
	const bgfx_view_id_t kHudViewId = 3;

	extern "C" void bgfxRenderHudPass()
	{
		if (!g_pGameLevel || !g_hud)
			return;

		g_hud->Render_Last();

		Fmatrix hudView, hudProj;
		hudView.build_camera_dir(Fvector().set(0.f, 0.f, 0.f), Device.vCameraDirection, Device.vCameraTop);

		const float fNear = -Device.mProject._43 / Device.mProject._33;
		float fFar = 100.f;
		if (_abs(Device.mProject._33 - 1.f) > EPS_L)
		{
			const float fFarDerived = fNear * Device.mProject._33 / (Device.mProject._33 - 1.f);
			if (fFarDerived > fNear + 1.f)
				fFar = fFarDerived;
		}
		hudProj.build_projection(psHUD_FOV, Device.fASPECT, HUD_VIEWPORT_NEAR, fFar);

		bgfx_set_view_rect(kHudViewId, 0, 0, (uint16_t)Device.dwWidth, (uint16_t)Device.dwHeight);
		bgfx_set_view_clear(kHudViewId, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
		bgfx_set_view_mode(kHudViewId, BGFX_VIEW_MODE_SEQUENTIAL);
		bgfx_set_view_transform(kHudViewId, hudView.m, hudProj.m);
		bgfx_touch(kHudViewId);

		bgfxRenderDynamic(1);
	}

	// ========================================================================
	// Skin-handoff implementation (hook declared in bgfxModelBridge.h:62, called
	// by the bridge's CPU pass on bgfxModelBridge.cpp:67).
	// ========================================================================
	// ========================================================================
	// Skinned dynamic models (CKinematics / CKinematicsAnimated, MT_SKELETON_*).
	//
	// CSkeletonX_ext::_Load_hw (FSkinned.cpp:591) already repacked the mesh into
	// the vertHW_1W..4W streams, whose bone references are pre-multiplied
	// indices (bone*3) - exactly what the reference CSkeletonX::_Render feeds
	// into sbones_array (SkeletonX.cpp:74-83). We bind those model buffers and
	// upload the same packing (3 rows per bone: M._11,M._21,M._31,M._41 / ...)
	// into u_bones; the vertex shader rebuilds the object-space skinned
	// position, u_modelViewProj applies the visual matrix.
	// ========================================================================
	bgfx_vertex_layout_t         s_skinLayoutDesc[4];
	bool                         s_skinLayoutInit[4] = { false, false, false, false };
	bgfx_vertex_layout_handle_t  s_skinLayout[4] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
							 BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
	xr_map<const void*, bgfx_vertex_buffer_handle_t> s_modelVbh;
	xr_map<const void*, bgfx_index_buffer_handle_t>  s_modelIbh;
	bgfx_uniform_handle_t s_skinBones = BGFX_INVALID_HANDLE;
	bgfx_uniform_handle_t s_skinSampler = BGFX_INVALID_HANDLE;

	bgfx_vertex_layout_handle_t bgfxSkinLayoutGet(u32 mode)
	{
		if (mode > 3)
			return BGFX_INVALID_HANDLE;
		if (s_skinLayoutInit[mode])
			return s_skinLayout[mode];

		bgfx_vertex_layout_t& d = s_skinLayoutDesc[mode];
		bgfx_vertex_layout_begin(&d, BGFX_RENDERER_TYPE_DIRECT3D11);
		bgfx_vertex_layout_add(&d, BGFX_ATTRIB_POSITION, 4, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		bgfx_vertex_layout_add(&d, BGFX_ATTRIB_NORMAL, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
		bgfx_vertex_layout_add(&d, BGFX_ATTRIB_TANGENT, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
		bgfx_vertex_layout_add(&d, BGFX_ATTRIB_BITANGENT, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
		bgfx_vertex_layout_add(&d, BGFX_ATTRIB_TEXCOORD0, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		if (mode == 3) // vertHW_4W: indices as px4 color
			bgfx_vertex_layout_add(&d, BGFX_ATTRIB_TEXCOORD2, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
		else if (mode == 1 || mode == 2) // vertHW_2W/3W: tc.zw = bone indices
			bgfx_vertex_layout_add(&d, BGFX_ATTRIB_TEXCOORD2, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
		bgfx_vertex_layout_end(&d);

		s_skinLayout[mode] = bgfx_create_vertex_layout(&d);
		s_skinLayoutInit[mode] = true;
		LogInfo("DYN skin layout mode=%u stride=%u id=%u", mode,
			(u32)bgfx_vertex_layout_get_stride(&d), s_skinLayout[mode].idx);
		return s_skinLayout[mode];
	}

	bgfx_vertex_buffer_handle_t bgfxModelVertexBuffer(ID3DVertexBuffer* vb, u32 mode)
	{
		if (!vb || vb->data.empty() || mode > 3)
			return BGFX_INVALID_HANDLE;
		const void* key = vb;
		auto it = s_modelVbh.find(key);
		if (it != s_modelVbh.end())
			return it->second;
		bgfxSkinLayoutGet(mode);
		const bgfx_memory_t* mem = bgfx_copy(&vb->data[0], (u32)vb->data.size());
		bgfx_vertex_buffer_handle_t h = bgfx_create_vertex_buffer(mem, &s_skinLayoutDesc[mode], 0);
		s_modelVbh[key] = h;
		return h;
	}

	bgfx_index_buffer_handle_t bgfxModelIndexBuffer(ID3DIndexBuffer* ib)
	{
		if (!ib || ib->data.empty())
			return BGFX_INVALID_HANDLE;
		const void* key = ib;
		auto it = s_modelIbh.find(key);
		if (it != s_modelIbh.end())
			return it->second;
		const bgfx_memory_t* mem = bgfx_copy(&ib->data[0], (u32)ib->data.size());
		bgfx_index_buffer_handle_t h = bgfx_create_index_buffer(mem, 0);
		s_modelIbh[key] = h;
		return h;
	}

	bool bgfxSkinDrawMesh(CSkeletonX* sk, CKinematics* K, const float* xform, u32 viewId = 0)
	{
		Fvisual* fv = dynamic_cast<Fvisual*>(sk);
		if (!fv)
			return false;
		const u32 rm = sk->SkinMode();
		if (rm > 4)
			return false;
		// RM_SINGLE meshes carry the single bone index in the vertex stream too,
		// so they render through the 1W program; only its bone count differs.
		const u32 mode = (rm == 0) ? 0u : (rm - 1u);
		const u32 boneCount = (rm == 0) ? (sk->SkinSingleBone() + 1u) : sk->SkinBoneCount();
		if (boneCount == 0 || boneCount > 85)
			return false;

		ID3DVertexBuffer* vb = fv->p_rm_Vertices;
		ID3DIndexBuffer*  ib = fv->p_rm_Indices;
		if (!vb || !ib)
			return false;

		u32 vCount = fv->vCount;
		u32 iBase  = fv->iBase;
		u32 iCount = fv->iCount;
		if (fv->Type == MT_SKELETON_GEOMDEF_PM)
		{
			FProgressive* pm = dynamic_cast<FProgressive*>(fv);
			FSlideWindow sw;
			if (!pm || !pm->GetCurrentSlideWindow(sw))
				return false;
			vCount = sw.num_verts;
			iBase += sw.offset;
			iCount = u32(sw.num_tris) * 3u;
		}
		if (!vCount || !iCount)
			return false;

		bgfx_vertex_layout_handle_t layout = bgfxSkinLayoutGet(mode);
		bgfx_program_handle_t prog = bgfxSkinProgramGet(mode);
		if (!bgfxIsValid(layout) || !bgfxIsValid(prog))
			return false;

		bgfx_vertex_buffer_handle_t vbh = bgfxModelVertexBuffer(vb, mode);
		bgfx_index_buffer_handle_t  ibh = bgfxModelIndexBuffer(ib);
		if (!bgfxIsValid(vbh) || !bgfxIsValid(ibh))
			return false;

		if (!bgfxIsValid(s_skinBones))
			s_skinBones = bgfx_create_uniform("u_bones", BGFX_UNIFORM_TYPE_VEC4, 255);
		if (!bgfxIsValid(s_skinSampler))
			s_skinSampler = bgfx_create_uniform("u_texture", BGFX_UNIFORM_TYPE_SAMPLER, 1);
		if (!bgfxIsValid(s_skinBones))
			return false;

		static xr_vector<float> boneRows;
		boneRows.clear();
		if (rm == 0)
		{
			const Fmatrix& M = K->LL_GetTransform_R(sk->SkinSingleBone());
			boneRows.reserve(85 * 12);
			for (u32 b = 0; b < 85; ++b)
			{
				boneRows.push_back(M._11); boneRows.push_back(M._21); boneRows.push_back(M._31); boneRows.push_back(M._41);
				boneRows.push_back(M._12); boneRows.push_back(M._22); boneRows.push_back(M._32); boneRows.push_back(M._42);
				boneRows.push_back(M._13); boneRows.push_back(M._23); boneRows.push_back(M._33); boneRows.push_back(M._43);
			}
			bgfx_set_uniform(s_skinBones, boneRows.data(), 255);
		}
		else
		{
			boneRows.reserve(boneCount * 12);
			for (u32 b = 0; b < boneCount; ++b)
			{
				const Fmatrix& M = K->LL_GetTransform_R(u16(b));
				boneRows.push_back(M._11); boneRows.push_back(M._21); boneRows.push_back(M._31); boneRows.push_back(M._41);
				boneRows.push_back(M._12); boneRows.push_back(M._22); boneRows.push_back(M._32); boneRows.push_back(M._42);
				boneRows.push_back(M._13); boneRows.push_back(M._23); boneRows.push_back(M._33); boneRows.push_back(M._43);
			}
			bgfx_set_uniform(s_skinBones, boneRows.data(), (uint16_t)(boneCount * 3u));
		}

		bgfx_texture_handle_t tex = bgfxWorldTextureGet(fv->diffuse_name.c_str());
		if (!bgfxIsValid(tex))
			tex = bgfxUIWhiteTextureGet();
		if (bgfxIsValid(s_skinSampler))
			bgfx_set_texture(0, s_skinSampler, tex, UINT32_MAX);

		bgfx_set_vertex_buffer_with_layout(0, vbh, fv->vBase, vCount, layout);
		bgfx_set_index_buffer(ibh, iBase, iCount);
		bgfx_set_transform(xform, 1);
		const uint64_t st = BGFX_STATE_WRITE_RGB
			| BGFX_STATE_WRITE_A
			| BGFX_STATE_WRITE_Z
			| BGFX_STATE_DEPTH_TEST_LESS
			| BGFX_STATE_MSAA;
		bgfx_set_state(st, 0);
		bgfxSetFogUniforms();
		bgfx_submit(viewId, prog, 0, BGFX_DISCARD_ALL);
		return true;
	}

	extern "C" bool bgfxSubmitSkinnedVisual(const void* visual, const float* transform,
						int hud)
	{
		if (!visual || !transform)
			return false;

		dxRender_Visual* v = static_cast<dxRender_Visual*>(const_cast<void*>(visual));
		switch (v->Type)
		{
		case MT_SKELETON_ANIM:
		case MT_SKELETON_RIGID:
		{
			CKinematics* K = static_cast<CKinematics*>(v);
			K->CalculateBones(TRUE);

			u32 drawn = 0, skipped = 0, modes = 0;
			for (dxRender_Visual* ch : K->children)
			{
				CSkeletonX* sk = dynamic_cast<CSkeletonX*>(ch);
				if (sk)
					modes |= 1u << sk->SkinMode();
				if (sk && bgfxSkinDrawMesh(sk, K, transform, hud != 0 ? kHudViewId : 0u))
					++drawn;
				else
					++skipped;
			}

			static u32 s_logged = 0;
			if (s_logged < 8)
			{
				++s_logged;
				LogInfo("DYN SKEL: '%s' children=%u drawn=%u skipped=%u bones=%u modes=0x%x",
					v->dbg_name.c_str(), (u32)K->children.size(), drawn, skipped,
					(u32)K->LL_BoneCount(), modes);
			}
			return drawn > 0;
		}
		default:
			// rigid/progressive dynamics (own FVF streams) are not ported yet
			return false;
		}
	}

	void bgfxDumpLevelGeom()
	{
		// --- bgfxport: inventory of level.geom (shared static geometry)
		IReader* g = FS.r_open("$level$", "level.geom");
		if (!g)
		{
			LogInfo("GEOM: level.geom NOT FOUND");
			return;
		}
		LogInfo("GEOM: total=%u bytes", (u32)g->length());
		dumpChunkIterator("GEOM", g, 64);
		// probe key chunks by id: 9=VB 10=IB 11=SWIS
		static const u32 ids[] = { 9, 10, 11 };
		for (int p = 0; p < 3; p++)
		{
			IReader* c = g->open_chunk(ids[p]);
			if (!c) continue;
			LogInfo("GEOM id=%u len=%u", ids[p], (u32)c->length());
			dumpHex("GEOMh", c, 128);
			c->close();
		}
		FS.r_close(g);
	}
}

// ============================================================================
// Console/global variables that used to live in xrRender_console.cpp
// ============================================================================
int		g_bRendering			= 0;
float	ps_r__Tree_w_rot	= 10.0f;
float	ps_r__Tree_w_speed	= 1.00f;
float	ps_r__Tree_w_amp	= 0.005f;
Fvector	ps_r__Tree_Wave		= {0.1f, 0.01f, 0.11f};
float	ps_r__Tree_SBC		= 1.5f;

// ============================================================================
// Environment pass (sky wiring): called from bgfxRenderInterface::Render().
// Order mirrors R2: RenderSky/RenderClouds before the world (combine :83/86),
// RenderFlares over the scene (combine :398) + RenderLast after sorted
// (forward :446). Guarded for menu (no level) — CEnvironment also guards
// internally, but eff_* may be null before the first level load.
// ============================================================================
extern "C" void bgfxRenderEnvironmentSky()
{
	if (!g_pGameLevel || !g_pGamePersistent)
		return;
	CEnvironment& env = g_pGamePersistent->Environment();
	env.RenderSky();
	env.RenderClouds();
}

extern "C" void bgfxRenderEnvironmentFx()
{
	if (!g_pGameLevel || !g_pGamePersistent)
		return;
	CEnvironment& env = g_pGamePersistent->Environment();
	env.RenderFlares();
	env.RenderLast();
}
