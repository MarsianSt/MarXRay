#include "stdafx.h"
#pragma hdrstop

#include "sh_atomic.h"

#include "dxRenderDeviceRender.h"

// Atomic - BGFX stubs: just release the (possibly null) D3D object pointers.
SVS::SVS() :
	vs(0)
#ifdef USE_DX11
//	,signature(0)
#endif	//	USE_DX11
{
	;
}


SVS::~SVS()
{
#ifdef USE_DX11
	//_RELEASE(signature);
#endif	//	USE_DX11
	_RELEASE(vs);
}


///////////////////////////////////////////////////////////////////////
//	SPS
SPS::~SPS								()			{	_RELEASE(ps);	}


SState::~SState							()			{	_RELEASE(state);	}


///////////////////////////////////////////////////////////////////////
//	SDeclaration
SDeclaration::~SDeclaration()
{	
#ifdef USE_DX11
	xr_map<ID3DBlob*, ID3DInputLayout*>::iterator iLayout;
	iLayout = vs_to_layout.begin();
	for( ; iLayout != vs_to_layout.end(); ++iLayout)
	{
		//	Release vertex layout
		_RELEASE(iLayout->second);
	}
#else	//	USE_DX11
	//	Release vertex layout
	_RELEASE(dcl);
#endif	//	USE_DX11
}
