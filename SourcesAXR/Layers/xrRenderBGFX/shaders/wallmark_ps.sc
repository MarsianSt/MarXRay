$input v_color0, v_texcoord0, v_viewPos, v_viewNormal

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

SAMPLER2D(s_wallmark, 0);

// Decal material id: deffer_base_aref_flat.ps:92-95 passes xmaterial for the
// wallmark/decal pass, same as the static class (common.h:17-18).
const float GBUF_MTL = 0.25;

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
    gl_FragData[0] = res;
    // Position G-buffer (Anomaly gbuf position): view-space position. The mark
    // blends with DestColor/SrcColor, which also scales this attachment.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7). Hemi comes
    // from u_gbufHemi because the reference's decal hemi (I.Nh.w,
    // deffer_base_flat.ps:37) is not available on a mark quad.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, u_gbufHemi.x);
}
