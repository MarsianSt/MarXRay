$input v_texcoord0, v_texcoord1, v_fogDepth

#include <bgfx_shader.sh>

uniform vec4 u_alphaCtrl;
uniform vec4 u_dtScale;
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

SAMPLER2D(u_texture, 0);
SAMPLER2D(u_mask,   1);
SAMPLER2D(u_dt0,    2);
SAMPLER2D(u_dt1,    3);
SAMPLER2D(u_dt2,    4);
SAMPLER2D(u_dt3,    5);

void main()
{
    vec4 base = texture2D(u_texture, v_texcoord0);
    vec3 color = base.rgb;
    if (u_dtScale.x > 0.5)
    {
        vec4 mask = texture2D(u_mask, v_texcoord0);
        mask /= max(mask.r + mask.g + mask.b + mask.a, 0.00001);
        vec2 uv = v_texcoord0 * u_dtScale.xy;
        vec3 det = texture2D(u_dt0, uv).rgb * mask.r
                 + texture2D(u_dt1, uv).rgb * mask.g
                 + texture2D(u_dt2, uv).rgb * mask.b
                 + texture2D(u_dt3, uv).rgb * mask.a;
        color = 2.0 * base.rgb * det;
    }
    // Forward fog removed: single SSFX screenspace layer in combine (Anomaly).
    gl_FragColor = vec4(color, 1.0);
}
