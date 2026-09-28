$input v_color0, v_texcoord0, v_fog

// AXR r3/particle.ps - the pixel stage of CBlender_Particle's BLEND / MUL /
// MUL_2X / ALPHA-ADD passes (Blender_Particle.cpp:127-131) and of every
// particles_*.s material whose PS is "particle". One SV_Target only
// (r3/particle.ps:22 ": SV_Target"): the forward pass runs after the combine
// (r4_rendertarget_phase_combine.cpp:374-388 -> r4_R_render.cpp:617-640), so
// there is no G-buffer to write and no deferred resolve behind it.
#include <bgfx_shader.sh>

SAMPLER2D(s_base, 0);

void main()
{
    // r3/particle.ps:25 - half4 result = I.c * s_base.Sample(smp_base, I.tc0)
    vec4 result = v_color0 * texture2D(s_base, v_texcoord0);

    // r3/particle.ps:48 - clip(result.a - (0.01f/255.0f)). The forward particle
    // cut: everything below the 8-bit zero point is dropped, which is what keeps
    // the blended quads from writing a grey halo over the whole billboard.
    // Written as the discard the rest of the port uses for the AXR aref
    // (world_solid_ps.sc:32, wallmark_ps.sc), the same test with the same
    // threshold, because the GLSL clip() has no fixed-function counterpart the
    // shaderc HLSL path would take.
    if (result.a - (0.01 / 255.0) < 0.0)
        discard;

    // r3/particle.ps:50 - result.w *= I.fog. The material blends
    // src_alpha/inv_src_alpha, so fog reaches the frame as coverage, not as a
    // colour: the particle fades into the fog instead of being tinted by it.
    // particle_add.ps:43 does the same to rgb, which is why the additive variant
    // needs its own program.
    result.w *= v_fog;

    gl_FragColor = result;
}
