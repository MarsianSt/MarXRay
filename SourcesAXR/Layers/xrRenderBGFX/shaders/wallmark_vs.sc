$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    // Wallmarks are coplanar with the surface; bias them towards the camera
    // (same trick as world_decal_vs) so they win the depth test against the
    // base geometry without z-fighting.
    gl_Position.z -= (0.02 / max(gl_Position.w, 0.01)) + 0.002;
    v_color0    = a_color0;
    v_texcoord0 = a_texcoord0;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    v_viewPos   = viewPos.xyz;
    // The reference draws wallmarks through the decal pair
    // (deffer_base_aref_flat.ps / deffer_model_flat_d.vs), which takes the
    // normal and the hemi from the decal mesh's own v_static.Nh. The port's
    // wallmark quad (bgfxWallMarks.cpp) carries position, colour and uv only,
    // so world-up stands in; stage 2 owns the real source.
    v_viewNormal = mul((mat3)u_modelView, vec3(0.0, 0.0, 1.0));
}
