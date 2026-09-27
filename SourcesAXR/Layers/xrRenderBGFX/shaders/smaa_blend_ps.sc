$input v_texcoord0

#include <bgfx_shader.sh>

#define SMAA_GLSL_3
#define SMAA_PRESET_ULTRA
#define SMAA_RT_METRICS u_smaaMetrics
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1

uniform vec4 u_smaaMetrics;

SAMPLER2D(s_smaaEdges, 0);
SAMPLER2D(s_smaaArea, 1);
SAMPLER2D(s_smaaSearch, 2);

#include <smaa_axr.h>

void main()
{
    float2 pixcoord = v_texcoord0 * u_smaaMetrics.zw;
    float4 offset[3];
    SMAABlendingWeightCalculationVS(v_texcoord0, pixcoord, offset);
    gl_FragColor = SMAABlendingWeightCalculationPS(v_texcoord0, pixcoord, offset, s_smaaEdges, s_smaaArea, s_smaaSearch, vec4(0.0));
}
