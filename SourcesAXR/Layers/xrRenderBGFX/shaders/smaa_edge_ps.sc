$input v_texcoord0

#include <bgfx_shader.sh>

#define SMAA_GLSL_3
#define SMAA_PRESET_ULTRA
#define SMAA_RT_METRICS u_smaaMetrics
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1

uniform vec4 u_smaaMetrics;

SAMPLER2D(s_smaaImage, 0);

#include <smaa_axr.h>

void main()
{
    float4 offset[3];
    SMAAEdgeDetectionVS(v_texcoord0, offset);
    gl_FragColor = vec4(SMAAColorEdgeDetectionPS(v_texcoord0, offset, s_smaaImage), 0.0, 0.0);
}
