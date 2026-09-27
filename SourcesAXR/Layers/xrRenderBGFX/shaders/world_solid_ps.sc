$input v_texcoord0, v_viewPos

#include <bgfx_shader.sh>

uniform vec4 u_alphaCtrl;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);

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
}
