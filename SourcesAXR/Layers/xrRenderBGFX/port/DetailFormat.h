#pragma once

#ifndef _DETAIL_FORMAT_H_
#define _DETAIL_FORMAT_H_
#pragma pack(push,1)

#define DETAIL_VERSION		3
#define DETAIL_SLOT_SIZE	2.f
#define DETAIL_SLOT_SIZE_2	DETAIL_SLOT_SIZE*0.5f

#define DO_NO_WAVING	0x0001

struct DetailHeader
{
	u32		version;
	u32		object_count;
	int		offs_x,	offs_z;
	u32		size_x,	size_z;
};
struct DetailPalette
{
	u16		a0:4;
	u16		a1:4;
	u16		a2:4;
	u16		a3:4;
};
struct DetailSlot
{
	u32				y_base	:	12;
	u32				y_height:	8;
	u32				id0		:   6;
	u32				id1		:	6;
	u32				id2		:	6;
	u32				id3     :	6;
	u32				c_dir	:	4;
	u32				c_hemi	:	4;
	u32				c_r		:	4;
	u32				c_g		:	4;
	u32				c_b		:	4;
	DetailPalette	palette [4];
public:
	enum			{	ID_Empty	= 0x3f	};
public:
	void			w_y		(float base, float height)
	{
		s32	_base	= iFloor((base + 200)/.2f);			clamp(_base,	0,4095);	y_base		= _base;
		f32 _error	= base - r_ybase();
		s32	_height = iCeil ((height+_error) / .1f);	clamp(_height,	0,255);		y_height	= _height;
	}

	float			r_ybase		()						{	return float(y_base)*.2f - 200.f;								}
	float			r_yheight	()						{	return float(y_height)*.1f;									}
	u32				w_qclr		(float v, u32 range)	{	s32 _v = iFloor(v * float(range)); clamp(_v,0,s32(range)); return _v; };
	float			r_qclr		(u32 v,   u32 range)	{	return float(v)/float(range); }
    u8				r_id		(u32 idx) {
        switch(idx)	{
        case 0: return (u8)id0;
        case 1: return (u8)id1;
        case 2: return (u8)id2;
        case 3: return (u8)id3;
        default: NODEFAULT;
        }
#ifdef DEBUG
		return 0;
#endif
    }
    void			w_id		(u32 idx, u8 val) {
        switch(idx){
        case 0: id0=val; break;
        case 1: id1=val; break;
        case 2: id2=val; break;
        case 3: id3=val; break;
        default: NODEFAULT;
        }
    }
};

#pragma pack(pop)
#endif
