#ifndef bgfxVisualCompat_included
#define bgfxVisualCompat_included
#pragma once

#include "bgfxVisualTypes.h"

// Replacement for D3DXCreateVertexBuffer / CreateIndexBuffer - allocates our
// light-weight CPU-side buffer objects (later uploaded to bgfx).
ID3DVertexBuffer* bgfxCreateVertexBuffer(const void* data, u32 count, u32 stride);
ID3DVertexBuffer* bgfxCreateVertexBufferEmpty(u32 count, u32 stride);
ID3DIndexBuffer*  bgfxCreateIndexBuffer(const void* data, u32 count);
ID3DIndexBuffer*  bgfxCreateIndexBufferEmpty(u32 count);

// CPU-only buffers sized by raw byte count (D3D9-style create-then-Lock/CopyMemory)
ID3DVertexBuffer* bgfxCreateBufferBytes(u32 bytes);
ID3DIndexBuffer*  bgfxCreateIndexBufferBytes(u32 bytes);

#endif // bgfxVisualCompat_included