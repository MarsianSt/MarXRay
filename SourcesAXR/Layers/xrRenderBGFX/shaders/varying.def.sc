vec3 a_position  : POSITION;
vec4 a_color0    : COLOR0;
vec4 a_color1    : COLOR1;
vec4 a_normal    : NORMAL;
vec4 a_tangent   : TANGENT;
vec4 a_bitangent : BITANGENT;
vec2 a_texcoord0 : TEXCOORD0;
vec2 a_texcoord1 : TEXCOORD1;
vec4 a_texcoord2 : TEXCOORD2;

vec4 v_color0    : COLOR0    = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_color1    : COLOR1    = vec4(0.0, 0.0, 0.0, 0.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec2 v_texcoord1 : TEXCOORD1 = vec2(0.0, 0.0);
vec3 v_worldPos  : TEXCOORD2 = vec3(0.0, 0.0, 0.0);
vec3 v_dir       : TEXCOORD3 = vec3(0.0, 0.0, 0.0);
// v_fogDepth is intentionally absent: no fragment shader reads it any more (the fog
// moved into combine_ps), and bgfx refuses to build a program whose vertex outputs and
// fragment inputs differ (bgfx_p.h:6218 "Vertex shader output doesn't match fragment
// shader input"), so an unused output breaks program creation.
// View-space position written into the position G-buffer attachment (SV_TARGET1).
// Anomaly r3 carries the same thing in gbuf position (combine_1.ps:194-201 reads P.xyz
// straight from it); the bgfx port keeps P_view on a dedicated RGBA16F attachment.
vec3  v_viewPos  : TEXCOORD5 = vec3(0.0, 0.0, 0.0);
// View-space normal that feeds the packed-normal half of the AXR G-buffer
// attachment (gbuffer_stage.h:7 XY, packed by gbuf_pack_normal). TEXCOORD6 is
// the next free semantic; TEXCOORD4 was v_fogDepth, removed with the forward fog.
vec3  v_viewNormal : TEXCOORD6 = vec3(0.0, 0.0, 1.0);
