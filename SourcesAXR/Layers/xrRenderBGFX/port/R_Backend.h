#ifndef r_backendH
#define r_backendH
#pragma once

// ============================================================================
// BGFX port: minimal CBackend stub.
// The original R_Backend.h wired the render backend to actual D3D state
// (shaders, buffers, render targets). BGFX does its own state management, so
// every backend method is a NO-OP here - ported visual classes keep all of
// their RCache.* calls, but they simply do nothing.
//
// RImplementation (CRender) is provided by bgfxRenderCompat.h
// ============================================================================

#include "Shader.h"
#include "r_constants.h"
#include "../../../xrEngine/fmesh.h"

#include "bgfxRenderCompat.h"

// ----------------------------------------------------------------------------
// Detailed statistic (rendering is stubbed, counters are inert)
// ----------------------------------------------------------------------------
struct R_statistics_element
{
	u32		verts, dips;
	ICF void	add(u32 _verts)		{ verts += _verts; dips++; }
};
struct R_statistics
{
	R_statistics_element	s_static;
	R_statistics_element	s_flora;
	R_statistics_element	s_flora_lods;
	R_statistics_element	s_details;
	R_statistics_element	s_ui;
	R_statistics_element	s_dynamic;
	R_statistics_element	s_dynamic_sw;
	R_statistics_element	s_dynamic_inst;
	R_statistics_element	s_dynamic_1B;
	R_statistics_element	s_dynamic_2B;
	R_statistics_element	s_dynamic_3B;
	R_statistics_element	s_dynamic_4B;
};

// ----------------------------------------------------------------------------
// CPU-side dynamic geometry streams (no GPU backing in the BGFX port)
// ----------------------------------------------------------------------------
class _VertexStream
{
public:
	ID3DVertexBuffer*		Buffer	()					{ return 0; }
	void*					Lock	(u32 cnt, u32 str, u32& offset)	{ static u8 fake[4096]; offset = 0; return fake; }
	void					Unlock	(u32 cnt, u32 str)					{}
	void					_clear	()									{}
	void					Destroy	()									{}
};
class _IndexStream
{
public:
	ID3DIndexBuffer*		Buffer	()					{ return 0; }
	void*					Lock	(u32 cnt, D3DFORMAT fmt, u32& offset) { static u8 fake[4096]; offset = 0; return fake; }
	void					Unlock	(u32 cnt, D3DFORMAT fmt)				{}
	void					_clear	()									{}
	void					Destroy	()									{}
};

// ----------------------------------------------------------------------------
// Matrices / hemi / tree backend buckets (inert)
// ----------------------------------------------------------------------------
class R_xforms
{
public:
	Fmatrix		m_w, m_v, m_p, m_xform_v;
	IC void		set		(u32 ID, const Fmatrix& M)		{}
	IC void		set_w	(const Fmatrix& M)				{ m_w = M; }
	IC void		set_v	(const Fmatrix& M)				{ m_v = M; }
	IC const Fmatrix& get	()							{ return m_w; }
	IC const Fmatrix& get_w	()							{ return m_w; }
	IC const Fmatrix& get_v	()							{ return m_v; }
	IC const Fmatrix& get_p	()							{ return m_p; }
};

class R_hemi
{
public:
	u32		scale, bias, sun;
};

class R_tree
{
public:
	IC void set_m_xform_v	(const Fmatrix& m)	{}
	IC void set_m_xform		(const Fmatrix& m)	{}
	IC void set_consts		(float a, float b, float c, float d)	{}
	IC void set_wave		(const Fvector4&)	{}
	IC void set_wind		(const Fvector4&)	{}
	IC void set_c_scale		(float, float, float, float) {}
	IC void set_c_bias		(float, float, float, float) {}
	IC void set_c_sun		(float, float, float, float) {}
};

// ----------------------------------------------------------------------------
// Backend stub
// ----------------------------------------------------------------------------
class CBackend
{
public:
	// Dynamic geometry streams
	_VertexStream				Vertex;
	_IndexStream				Index;
	ID3DIndexBuffer*			QuadIB;
	ID3DIndexBuffer*			old_QuadIB;
	ID3DIndexBuffer*			CuboidIB;
	R_xforms					xforms;
	R_hemi						hemi;
	R_tree						tree;

	struct _stats
	{
		u32		polys, verts, calls, vs, ps, xforms, target_rt, target_zb;
		R_statistics		r;
	}	stat;

	CBackend()	{ QuadIB = 0; old_QuadIB = 0; CuboidIB = 0; }

	// xforms
	IC void						set_xform_world		(const Fmatrix& M)		{ xforms.set_w(M); }
	IC void						set_xform_view		(const Fmatrix& M)		{ xforms.set_v(M); }
	IC const Fmatrix&			get_xform_world		()						{ return xforms.m_w; }
	IC const Fmatrix&			get_xform_view		()						{ return xforms.m_v; }

	// shader / geometry
	IC void						set_Shader			(Shader* S, u32 pass = 0)	{}
	IC void						set_Shader			(ref_shader& S, u32 pass = 0)	{ set_Shader(&*S, pass); }
	IC void						set_Geometry		(SGeometry* _geom)			{}
	IC void						set_Geometry		(ref_geom& _geom)			{ set_Geometry(&*_geom); }
	IC bool						is_TessEnabled		()						{ return false; }

	// render
	IC void						Render				(u32 T, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC)	{}
	IC void						Render				(u32 T, u32 startV, u32 PC)	{}

	// constants
	ICF ref_constant			get_c				(LPCSTR n)								{ return ref_constant(); }
	ICF ref_constant			get_c				(shared_str& n)							{ return ref_constant(); }
	ICF void					set_c				(R_constant* C, const Fmatrix& A)		{}
	ICF void					set_c				(R_constant* C, const Fvector4& A)		{}
	ICF void					set_c				(R_constant* C, float x,float y,float z,float w) {}
	ICF void					set_ca				(R_constant* C, u32 e, const Fmatrix& A) {}
	ICF void					set_ca				(R_constant* C, u32 e, const Fvector4& A) {}
	ICF void					set_ca				(R_constant* C, u32 e, float x,float y,float z,float w) {}
	ICF void					set_c				(LPCSTR n, const Fmatrix& A)		{}
	ICF void					set_c				(LPCSTR n, const Fvector4& A)		{}
	ICF void					set_c				(LPCSTR n, float x,float y,float z,float w) {}
	ICF void					set_ca				(LPCSTR n, u32 e, const Fmatrix& A)	{}
	ICF void					set_ca				(LPCSTR n, u32 e, const Fvector4& A)	{}
	ICF void					set_ca				(LPCSTR n, u32 e, float x,float y,float z,float w) {}
	ICF void					set_c				(shared_str& n, const Fmatrix& A)		{}
	ICF void					set_c				(shared_str& n, const Fvector4& A)		{}
	ICF void					set_c				(shared_str& n, float x,float y,float z,float w)	{}
	ICF void					set_ca				(shared_str& n, u32 e, const Fmatrix& A) {}
	ICF void					set_ca				(shared_str& n, u32 e, const Fvector4& A) {}
	ICF void					set_ca				(shared_str& n, u32 e, float x,float y,float z,float w) {}

	ICF void					get_ConstantDirect	(shared_str& n, u32 DataSize, void** pVData, void* pGData = 0, void* pPData = 0) {}

	// debug draw (inert)
	void dbg_DrawOBB			(Fmatrix& T, Fvector& half_dim, u32 C)	{}
	void dbg_DrawLINE			(Fmatrix& T, Fvector& p1, Fvector& p2, u32 C)	{}
};

extern CBackend		RCache;

#endif // r_backendH