$input a_position, a_color0, a_texcoord0, a_texcoord1
$output v_color0, v_texcoord0, v_fogDepth
#include <bgfx_shader.sh>

uniform vec4 u_grassParams;
uniform vec4 u_grassInt;
uniform vec4 benders_setup;
uniform vec4 benders_pos[32];

void main()
{
    vec3 P = a_position;
    float H = a_texcoord1.x;

    float t = u_grassParams.x;
    float2 wp = P.xz;
    float wave = sin(t * 1.7 + wp.x * 0.37 + wp.y * 0.29)
               + 0.5 * sin(t * 3.1 + wp.x * 0.71 - wp.y * 0.63);
    P.xz += vec2(u_grassParams.z, u_grassParams.w) * (wave * u_grassParams.y * H);

    int nb = int(u_grassInt.x);
    for (int b = 0; b < 4; ++b)
    {
        if (b >= nb)
            break;

        vec3 bpos = benders_pos[b].xyz;
        float rstr = benders_pos[b].w;
        vec3 dir = benders_pos[b + 16].xyz;
        float str = benders_pos[b + 16].w;
        bool non_dynamic = rstr <= 0.0;
        float radius = non_dynamic ? benders_setup.x : rstr;
        float dist = distance(P.xz, bpos.xz);
        float hl = 1.0 - clamp(abs(P.y - bpos.y) / (non_dynamic ? 2.0 : rstr), 0.0, 1.0);
        hl *= H;
        float bend = 1.0 - clamp(dist / (radius + 0.001), 0.0, 1.0);
        vec3 bend_dir = normalize(P - bpos) * bend;
        float dir_limit = dir.y >= -1.0 ? clamp(dot(bend_dir, dir) * 5.0, 0.0, 1.0) : 1.0;
        P.xz += bend_dir.xz * dir_limit * 2.0 * str * hl;
        P.y  -= bend * 0.6 * str * hl * dir_limit;
    }

    vec4 viewPos = mul(u_modelView, vec4(P, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(P, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
    v_fogDepth = length(viewPos.xyz);
}
