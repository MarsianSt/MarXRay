$input v_texcoord0

#include <bgfx_shader.sh>
#include <gbuf_pack.h>

// Packed G-buffer attachment 2 of the scene FB.
SAMPLER2D(s_gbuf, 0);
// View-space position attachment 1. It is the "did anything draw here" mask:
// the sky and the clouds never write it, so it keeps the clear value and
// combine_ps already relies on exactly that (P == 0 => no fog).
SAMPLER2D(s_position, 1);
// HDR colour attachment 0, the albedo the port renders today.
SAMPLER2D(s_hdr, 2);
// x = view (0 normal, 1 depth, 2 hemi, 3 albedo), y = far plane for the depth ramp.
uniform vec4 u_gbufDebug;

void main()
{
    vec2 tc = v_texcoord0;
    vec3 P = texture2D(s_position, tc).xyz;
    if (dot(P, P) < 1e-6)
    {
        // Sky / clouds: report an empty attachment rather than the clear value,
        // so a correct frame is black in all four views.
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 G = texture2D(s_gbuf, tc);
    int view = int(u_gbufDebug.x + 0.5);
    vec3 c = vec3(0.0, 0.0, 0.0);
    if (view == 0)
    {
        // gbuffer_stage.h:59-65, decoded straight back into an RGB view.
        c = gbuf_unpack_normal(G.xy) * 0.5 + 0.5;
    }
    else if (view == 1)
    {
        // G.z is the view-space z pack_gbuffer() stored (gbuffer_stage.h:104),
        // negative in front of the camera; ramp it from black at the near plane
        // to white at the far plane.
        float depth = saturate(abs(G.z) / max(u_gbufDebug.y, 1.0));
        c = vec3(depth, depth, depth);
    }
    else if (view == 2)
    {
        float hemi = gbuf_unpack_hemi(G.w);
        c = vec3(hemi, hemi, hemi);
    }
    else
    {
        c = texture2D(s_hdr, tc).rgb;
    }
    gl_FragColor = vec4(c, 1.0);
}
