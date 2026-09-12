#include "stdafx.h"
#include "bgfxShaderCompiler.h"

#include <windows.h>

#include "bgfxRenderInterface.h"

namespace
{
	typedef int (__cdecl* PFN_shaderc_compile)(
		  const char* _inputFile
		, char _shaderType
		, const char* _platform
		, const char* _profile
		, const char* _varyingDef
		, const char* _includeDir
		, void** _outData
		, u32* _outSize
		, char* _errMsg
		, u32 _errSize
		);

	typedef void (__cdecl* PFN_shaderc_free)(void* _data);

	HMODULE                 s_shadercDll = nullptr;
	PFN_shaderc_compile     s_compile    = nullptr;
	PFN_shaderc_free        s_freeBlob   = nullptr;

	// Maps the active bgfx backend to shaderc --platform/-p.
	void GetTargetForBackend(const char*& _platform, const char*& _profile)
	{
		switch (bgfx_get_renderer_type())
		{
		case BGFX_RENDERER_TYPE_DIRECT3D12:
			_platform = "windows";
			_profile  = "s_6_0";
			break;
		case BGFX_RENDERER_TYPE_OPENGL:
			_platform = "linux";
			_profile  = "430";
			break;
		case BGFX_RENDERER_TYPE_DIRECT3D11:
		default:
			_platform = "windows";
			_profile  = "s_5_0";
			break;
		case BGFX_RENDERER_TYPE_VULKAN:
			_platform = "linux";
			_profile  = "spirv";
			break;
		}
	}

	bool EnsureLoaded()
	{
		if (s_compile)
			return true;

		s_shadercDll = LoadLibraryA("shaderc.dll");
		if (!s_shadercDll)
		{
			LogInfo("! [BGFX] shaderc.dll not found - UI/World programs will be missing.");
			return false;
		}

		s_compile  = (PFN_shaderc_compile)GetProcAddress(s_shadercDll, "shaderc_compile");
		s_freeBlob = (PFN_shaderc_free)GetProcAddress(s_shadercDll, "shaderc_mem_free_blob");
		if (!s_compile || !s_freeBlob)
		{
			LogInfo("! [BGFX] shaderc.dll exports missing (shaderc_compile=%p)", (void*)s_compile);
			FreeLibrary(s_shadercDll);
			s_shadercDll = nullptr;
			s_compile   = nullptr;
			s_freeBlob  = nullptr;
			return false;
		}
		return true;
	}
} // namespace

bool bgfxShaderCompileFile(const char* _scFile, char _type, std::vector<std::uint8_t>& _outBlob)
{
	_outBlob.clear();

	if (!EnsureLoaded())
		return false;

	// Resolve $game_shaders$\<path> to an absolute filesystem path.
	string_path fullPath;
	string_path includeDir;
	string_path varyingDef;
	{
		FS.update_path(fullPath, "$game_shaders$", _scFile);

		// Include dir for #include <bgfx_shader.sh>: the same folder the .sc
		// files live in (ship bgfx_shader.sh next to the sources).
		string_path dirOnly;
		xr_strcpy(dirOnly, fullPath);
		char* slash = strrchr(dirOnly, '\\');
		if (slash)
		{
			*slash = '\0';
			xr_strcpy(includeDir, dirOnly);
		}
		else
		{
			xr_strcpy(includeDir, dirOnly);
			xr_strcat(includeDir, "\\");
		}

		FS.update_path(varyingDef, "$game_shaders$", "varying.def.sc");
	}

	const char* platform;
	const char* profile;
	GetTargetForBackend(platform, profile);

	char errMsg[1024];
	errMsg[0] = '\0';

	void*  blob    = nullptr;
	u32    blobSize = 0;
	const int rc = s_compile(
		fullPath
		, _type
		, platform
		, profile
		, varyingDef
		, includeDir
		, &blob
		, &blobSize
		, errMsg
		, sizeof(errMsg)
		);

	if (rc != 0 || !blob || blobSize == 0)
	{
		LogInfo("! [BGFX] shaderc failed (%s/%s) for '%s': %s"
			, platform, profile, _scFile, errMsg[0] ? errMsg : "(no message)");
		if (blob && s_freeBlob)
			s_freeBlob(blob);
		return false;
	}

	_outBlob.assign((std::uint8_t*)blob, (std::uint8_t*)blob + blobSize);
	if (s_freeBlob)
		s_freeBlob(blob);

	LogInfo("[BGFX] compiled '%s' (%s/%s): %u bytes"
		, _scFile, platform, profile, blobSize);
	return true;
}