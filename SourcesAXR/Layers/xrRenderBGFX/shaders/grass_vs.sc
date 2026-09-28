$input a_position, a_color0, a_texcoord0, a_texcoord1, a_texcoord2
$output v_color0, v_texcoord0, v_viewPos, v_viewNormal
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

uniform vec4 u_grassParams;
uniform vec4 u_grassInt;
uniform vec4 u_grassAlign;
uniform vec4 benders_setup;
uniform vec4 benders_pos[32];

void main()
{
    vec3 P = a_position;
    float H = a_texcoord1.x;

    // deffer_grass.vs:45-46
    //   // Force grass to go up
    //   P.xz = P.xz - 0.5f * data.xz * H * grass_align;
    // data is exdata[i/4] and grass_align is ps_ssfx_terrain_grass_align
    // (dx10DetailManager_VS.cpp:178  RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align)).
    P.xz -= 0.5 * a_texcoord2.xz * H * u_grassAlign.x;

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
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // The reference takes the blade normal from a per-blade CPU constant, not
    // from the vertex stream:
    //   deffer_grass.vs:8    float4 exdata[61]; // Terrain Normal [xyz] & Grass alpha [w]
    //   deffer_grass.vs:24   float4 data = exdata[i / 4];
    //   deffer_grass.vs:96-97 // Use terrain normal [ data.xyz ]
    //                        float3 N = mul((float3x3)m_WV, data.xyz);
    // and the CPU fills that store with the world-space normal of the triangle
    // the blade stands on:
    //   DetailManager_Decompress.cpp:227  terrain_normal.mknormal(Tv[0], Tv[1], Tv[2]);
    //   DetailManager_Decompress.cpp:242  Item.normal = terrain_normal;
    //   dx10DetailManager_VS.cpp:264      c_ExData[dwBatch].set(Instance.normal.x, ...);
    // The port expands every blade into its own vertices and hands that same
    // per-blade value over in a_texcoord2.xyz, so this is the identical vector.
    // The old (0,0,1) world-Z stand-in was wrong on both counts: it is not the
    // reference vector, and with the +Y-up world of common_functions.h:101
    // (calc_model_hemi_r1 = max(0, n.y) * L_hemi_color) it left the grass hemi
    // and NdotL at zero for the whole class.
    v_viewNormal = mul((mat3)u_modelView, a_texcoord2.xyz);
}
