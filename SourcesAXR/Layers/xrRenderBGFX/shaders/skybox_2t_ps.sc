$input v_color0, v_dir, v_worldPos

#include <bgfx_shader.sh>

SAMPLERCUBE(s_sky0, 0);
SAMPLERCUBE(s_sky1, 1);
SAMPLER2D(s_tonemap, 2);

uniform vec4 u_fogColor;

void main()
{
    vec3 dir = normalize(v_dir);
    vec3 s0 = textureCube(s_sky0, dir);
    vec3 s1 = textureCube(s_sky1, dir);
    float scale = texture2D(s_tonemap, vec2(0.5, 0.5)).x;
    vec3 tint = v_color0.rgb * (scale * 2.0);
    vec3 sky = tint * mix(s0, s1, v_color0.a);
    sky *= 0.33;
    // Horizon color match: blend the grazing band toward the live weather
    // fog_color so the terrain/sky boundary has no seam under any weather.
    // True view-ray height (v_dir interpolates badly on box faces). Power 10:
    // gradual melt over ~10deg; 32 was too sharp and read as a contour line.
    vec3 vd = v_worldPos - u_invView[3].xyz;
    float h = vd.y / max(length(vd), 0.001);
    sky = mix(sky, u_fogColor.rgb, pow(1.0 - saturate(h), 10.0));
    gl_FragColor = vec4(sky, 1.0);
}
