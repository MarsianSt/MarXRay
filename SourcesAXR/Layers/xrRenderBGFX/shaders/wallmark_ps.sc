$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_wallmark, 0);

// Mirror of the reference effects\wallmark pixel shader (stub_default_ma):
//   res.rgb = lerp(tex.rgb, v_color.rgb, v_color.a);
//   res.a  *= v_color.a;
// Combined with the multiply blend (DestColor/SrcColor) the mark starts fully
// applied and fades towards neutral grey as its TTL runs out.
void main()
{
    vec4 res = texture2D(s_wallmark, v_texcoord0);
    res.rgb = mix(res.rgb, v_color0.rgb, v_color0.a);
    res.a *= v_color0.a;
    gl_FragColor = res;
}
