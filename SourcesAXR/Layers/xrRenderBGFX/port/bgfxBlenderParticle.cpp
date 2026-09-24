#include "stdafx.h"
#pragma hdrstop

#include "bgfxBlenderParticle.h"
#include "bgfxParticleRender.h"

namespace bgfxBlenderParticle
{

namespace
{
struct ShaderBlendEntry
{
	const char*	name;
	int			mode;
};

// oBlend 真值,见 game/gamedata/shaders.xr (B_PARTICLE 描述符):
// set=0 blend=1 add=2 dark=3 alpha_add=5.
// xadd/xblend/xdistort 在 shaders.xr 里是 S_SET 屏后类,无 oBlend;
// 此处按 billboard 语义回退:add->ADD,其余->BLEND(distort 暂不支持).
const ShaderBlendEntry kTable[] =
{
	{ "particles\\set",			bgfxParticles::BLEND_SET },
	{ "particles\\blend",		bgfxParticles::BLEND_BLEND },
	{ "particles\\add",			bgfxParticles::BLEND_ADD },
	{ "particles\\dark",		bgfxParticles::BLEND_MUL },
	{ "particles\\alpha_add",	bgfxParticles::BLEND_ALPHA_ADD },
	{ "particles\\xadd",		bgfxParticles::BLEND_ADD },
	{ "particles\\xblend",		bgfxParticles::BLEND_BLEND },
	{ "particles\\xdistort",	bgfxParticles::BLEND_BLEND },
};
}

int BlendModeForShader(const char* shaderName)
{
	if (shaderName && shaderName[0])
		for (const ShaderBlendEntry& e : kTable)
			if (0 == stricmp(e.name, shaderName))
				return e.mode;
	return bgfxParticles::BLEND_BLEND;
}

}
