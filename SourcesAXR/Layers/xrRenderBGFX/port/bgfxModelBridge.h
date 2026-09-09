#pragma once

// Lightweight C bridge between the (non-PCH) BGFX render interface and the
// ported model layer. Avoids pulling the heavy port headers (Shader.h,
// sh_atomic.h, ...) into non-port translation units that use PCH.

class IRenderVisual;
class IReader;

extern "C"
{
	void*	bgfxModelCreate		(const char* name);
	void*	bgfxModelCreateChild	(const char* name, IReader* data);
	void*	bgfxModelDuplicate	(void* V);
	void	bgfxModelDelete		(void** V, int bDiscard);
	void*	bgfxGetVisual		(int id);
	void	bgfxLoadVisuals		(IReader* fs);
	void	bgfxLoadGeometry	();
	void	bgfxRenderWorld		();
	void	bgfxDumpLevelGeom	();
}