$input v_color0, v_texcoord0, v_fog

// AXR r3/particle_add.ps - the pixel stage of the additive particle materials
// (particles_add.s:2 shader:begin("particle","particle_add"),
// particles_xadd.s:2 - the same pair, particles_xadd.s adds : distort(true) for
// its l_special slot). One SV_Target (r3/particle_add.ps:20), drawn in the
// forward pass after the combine, exactly as the blend variant.
#include <bgfx_shader.sh>

SAMPLER2D(s_base, 0);

void main()
{
    // r3/particle_add.ps:22 - float4 result = I.c * s_base.Sample(smp_base, I.tc0)
    vec4 result = v_color0 * texture2D(s_base, v_texcoord0);

    // r3/particle_add.ps:41 - clip(result.a - (0.01f / 255.0f)), the same
    // forward cut the blend variant applies, as the port's discard idiom
    // (world_solid_ps.sc:32, wallmark_ps.sc).
    if (result.a - (0.01 / 255.0) < 0.0)
        discard;

    // r3/particle_add.ps:42-43 - result.w *= I.fog; result.xyz *= I.fog.
    // The additive material blends one/one (particles_add.s:4), so its colour
    // survives the blend and the fog has to be spent on rgb as well; without
    // the rgb term a distant additive quad adds its full value on top of a fog
    // bank that is already at the fog colour, which reads as a bright haze.
    result.w   *= v_fog;
    result.xyz *= v_fog;

    gl_FragColor = result;
}
