# MarXRay BGFX Renderer Integration Notes

## Problem
The `xrRenderBGFX` project only contains bgfx/bx library source files. The actual renderer implementation
(IRenderFactory, IRenderDeviceRender, exports) is completely missing, causing crash at EngineAPI.cpp:68
when `RenderFactory->CreateRenderDeviceRender()` is called with NULL RenderFactory.

## Solution
Created complete renderer implementation with 18 new files. Build successful, game runs!

## Architecture
- Engine (xrEngine) loads renderer DLL at runtime via LoadLibrary("xrRenderBGFX")
- DLL must export: SupportsVulkanRendering() and set global pointers in DllMain:
  - RenderFactory (IRenderFactory*)
  - Render (IRender_interface*)
  - DU (CDUInterface*)
  - UIRender (IUIRender*)
  - DRender (IDebugRender*)

## C++ Standard Compatibility Issue
**CRITICAL**: XRay engine headers (xrCore.h) don't compile with C++20, but bgfx/bx headers require C++20.
Solution:
- Use C++17 for most files
- Use C++20 only for bgfx source files (bgfx_src/*.cpp, bx_src/amalgamated.cpp)
- Use bgfx C API (bgfx_capi.h) instead of C++ API for C++17 compatibility

## Project Structure
```
SourcesAXR/Layers/xrRenderBGFX/    <- BGFX renderer project
SourcesAXR/Layers/xrRender/        <- Original DX renderer (reference)
SourcesAXR/Include/xrRender/       <- Interface headers
game/gamedata/shaders/bgfx/        <- Shader output directory
```

## Build Configuration
- Debug: BGFX_CONFIG_RENDERER_DIRECT3D11=1, others disabled
- Release: same as debug
- Output: SourcesAXR/OutputDirectory/Binaries/Release/xrRenderBGFX.dll
- Copy to: game/bin/xrRenderBGFX.dll
- Dependencies: bgfxRelease.lib, bimgRelease.lib, bimg_decodeRelease.lib, xrAPI.lib, xrCore.lib

## Implementation Files
- stdafx.h/.cpp - Precompiled header with xrCore.h
- xrRenderBGFX.cpp - DllMain + exports + global pointers
- bgfxRenderDeviceRender.h/.cpp - Main render device (uses C API)
- bgfxRenderFactory.h/.cpp - Factory for all render objects
- bgfx_capi.h - Minimal bgfx C API declarations for C++17
- bgfx*.h/.cpp - Stub implementations for all render interfaces

## Key Settings in vcxproj
- Project-wide: stdcpp17
- bgfx_src/*.cpp: stdcpp20 (per-file override)
- PrecompiledHeader: Use for most files, NotUsing for bgfx source files
- AdditionalLibraryDirectories: $(SolutionDir)..\OutputDirectory\Libraries\Release
- AdditionalDependencies: xrAPI.lib, xrCore.lib, bgfx libs

## Build Command
```batch
"D:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  "E:\XRay-engine\MarXRay\SourcesAXR\engine.sln" ^
  /p:Configuration=Release /p:Platform=x64 /t:xrRenderBGFX
```

## Shader Pipeline
- Shaders go in game/gamedata/shaders/bgfx/
- bgfx uses its own shader language (GLSL-like, compiled with shaderc)
- Need to convert/fetch compiled .bin shaders or compile from HLSL/GLSL

## Key Files Reference
- EngineAPI.cpp: LoadLibrary("xrRenderBGFX"), calls Device.ConnectToRender()
- Device_create.cpp: ConnectToRender() calls RenderFactory->CreateRenderDeviceRender()
- RenderFactory.h: IRenderFactory interface with all Create/Destroy methods
- RenderDeviceRender.h: IRenderDeviceRender interface (device, states, etc.)

## Status
- [x] Renderer DLL builds successfully
- [x] Game launches without crash
- [x] Renderer loads correctly (log shows "Available render modes[2]: renderer_bgfx")
- [ ] Actual rendering (needs shader pipeline)
- [ ] Full game functionality
