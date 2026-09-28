$input a_position, a_texcoord0, a_texcoord1
$output v_texcoord0, v_viewPos, v_viewNormal, v_hemi
#include <bgfx_shader.sh>
#include <gbuf_pack.h>

// deffer_grass.vs:8   float4 exdata[61];  // Terrain Normal [xyz] & Grass alpha [w]
// deffer_grass.vs:14  float4 array[61*4];
// Both are vertex-constant stores of the reference detail manager, filled per
// batch by DetailManager_VS.cpp:278-294 (the 3x4 scaled matrix rows and the
// colour row) and dx10DetailManager_VS.cpp:263-264 (terrain normal + alpha).
// hw_BatchSize is 61 there (DetailManager_VS.cpp:52-53), which is why the
// arrays are that size and why a submit holds at most that many blades.
uniform vec4 c_exdata[61];
uniform vec4 c_array[61*4];
uniform vec4 u_grassParams;
uniform vec4 u_grassInt;
uniform vec4 u_grassAlign;
uniform vec4 benders_setup;
uniform vec4 benders_pos[32];

void main()
{
    // deffer_grass.vs:27-34
    //   int  i = v.misc.w;              // the per-vertex matrix id
    //   float4 m0 = array[i+0];  m1 = array[i+1];  m2 = array[i+2];  c0 = array[i+3];
    //   float4 data = exdata[i / 4];
    int i = int(a_texcoord1.y + 0.5);
    float4 m0 = c_array[i + 0];
    float4 m1 = c_array[i + 1];
    float4 m2 = c_array[i + 2];
    float4 c0 = c_array[i + 3];
    float4 data = c_exdata[i / 4];

    // deffer_grass.vs:36-46 - the world position out of the constant matrix, the
    // vertex height above the instance origin, and the "force grass to up" shift
    // along the terrain normal (grass_align = ps_ssfx_terrain_grass_align,
    // dx10DetailManager_VS.cpp:178).
    vec4 vp = vec4(a_position, 1.0);
    vec3 P = vec3(dot(m0, vp), dot(m1, vp), dot(m2, vp));
    float H = P.y - m1.w;
    P.xz -= 0.5 * data.xz * H * u_grassAlign.x;

    // a_texcoord1.x is v_detail.misc.z, the fraction of the model bounding box
    // height (DetailManager_VS.cpp:105). The reference spends it on the wave
    // together with `consts` / `wave` / `dir2D` (deffer_grass.vs:48-58,
    // hw_Render_dump's three per-frame stores, DetailManager.cpp:455-458); those
    // stores and the still/wave0/wave1 split are not bound here, see bgfxDetails.cpp.
    float2 wp = P.xz;
    float t = u_grassParams.x;
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

    // deffer_grass.vs:116-119
    //   float3 Pe = mul(m_V, pos);
    //   O.position = float4(Pe, hemi);
    vec4 viewPos = mul(u_modelView, vec4(P, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(P, 1.0));
    v_texcoord0 = a_texcoord0;
    // Position G-buffer: view-space position, consumed by combine_ps (AXR gbuf P.xyz).
    v_viewPos = viewPos.xyz;
    // deffer_grass.vs:96-97
    //   // Use terrain normal [ data.xyz ]
    //   float3 N = mul((float3x3)m_WV, data.xyz);
    v_viewNormal = mul((mat3)u_modelView, data.xyz);
    // deffer_grass.vs:115
    //   float hemi = clamp(c0.w, 0.05f, 1.0f);
    // c0.w is Instance.c_hemi, the quantized per-slot hemisphere value
    // (DetailManager_Decompress.cpp:307  Item.c_hemi = DS.r_qclr(DS.c_hemi, 15),
    // stored by DetailManager_VS.cpp:294 / dx10DetailManager_VS.cpp:274).
    v_hemi = clamp(c0.w, 0.05, 1.0);
}
