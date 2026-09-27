$input v_texcoord0, v_viewPos, v_viewNormal

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_alphaCtrl;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

// xmaterial with USE_R2_STATIC_SUN (common.h:17-18); the static and decal
// classes share it in the reference (deffer_base_flat.ps:19, :54).
const float GBUF_MTL = 0.25;

void main()
{
    vec4 c = texture2D(u_texture, v_texcoord0);
    if (u_alphaCtrl.y > 0.5 && c.a < u_alphaCtrl.x)
        discard;
    // Forward fog removed: screenspace fog in combine is the single layer
    // (as in Anomaly); keeping both gives 2f-f^2 overfog.
    // gl_FragData[0] instead of gl_FragColor: once the PS declares a second
    // output shaderc stops emitting the gl_FragColor/SV_TARGET0 alias.
    gl_FragData[0] = c;
    // Position G-buffer (Anomaly gbuf position): view-space position.
    gl_FragData[1] = vec4(v_viewPos, 1.0);
    // Packed G-buffer, AXR f_deffer::position (gbuffer_stage.h:7):
    // XY = packed normal, Z = view-space z, W = hemi.
    gl_FragData[2] = gbuf_pack_gbuffer(normalize(v_viewNormal), v_viewPos.z, u_gbufHemi.x);
}
