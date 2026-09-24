#pragma once

// BGFX detail-objects (grass/clutter) subsystem.
// X-Ray bakes detail objects into level.details (chunk0 header, chunk1 models,
// chunk2 slots). The BGFX layer has no CDetailManager, so this module loads the
// database, decompresses the slots around the camera and submits the expanded
// geometry through the grass program. CPU pipeline mirrors the reference
// (Layers/xrRender DetailManager{,_CACHE,_Decompress}.cpp).

void	bgfxDetailsLoad();
void	bgfxDetailsUnload();
void	bgfxDetailsRender();
