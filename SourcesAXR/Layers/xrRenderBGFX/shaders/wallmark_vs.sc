$input a_position, a_texcoord0
$output v_texcoord0
#include <bgfx_shader.sh>

void main()
{
    // AXR wmark.vs:18 - o.hpos = mul(m_VP, P), P in world coords, and nothing
    // else reaches the pixel stage. The reference vertex stage also runs
    // wmark_shift() (shared/wmark.h), which needs the surface normal the port's
    // quad does not carry, so the coplanar bias stays the clip-space one the
    // decal path uses (world_decal_vs.sc:12) - it serves the same purpose as
    // wmark_shift's normal offset: winning the depth test against the base
    // geometry without z-fighting.
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    gl_Position.z -= (0.02 / max(gl_Position.w, 0.01)) + 0.002;
    v_texcoord0 = a_texcoord0;
}
