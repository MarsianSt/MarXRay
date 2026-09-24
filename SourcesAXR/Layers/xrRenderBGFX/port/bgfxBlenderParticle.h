#pragma once

// 装配层 Blender_Particle 对接:粒子 shader 名 -> billboard blend mode.
// 真值来源:game/gamedata/shaders.xr 内 B_PARTICLE 描述符的 oBlend
// (set=0 blend=1 add=2 dark=3 alpha_add=5).
// bgfx 无 blender 编译体系,blend 不走 Shader,改由 SubmitPAPI 逐调用传入,
// 故在此按 def->m_ShaderName 查表,查不到回默认 BLEND.
namespace bgfxBlenderParticle
{
int BlendModeForShader(const char* shaderName);
}
