#pragma once
// Minimal bgfx C API declarations for C++17 compatibility
// This avoids including bgfx.h which requires C++20

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// bgfx handle type
typedef struct bgfx_handle { uint16_t idx; } bgfx_handle_t;
typedef bgfx_handle_t bgfx_dynamic_index_buffer_handle_t;
typedef bgfx_handle_t bgfx_dynamic_vertex_buffer_handle_t;
typedef bgfx_handle_t bgfx_frame_buffer_handle_t;
typedef bgfx_handle_t bgfx_index_buffer_handle_t;
typedef bgfx_handle_t bgfx_indirect_buffer_handle_t;
typedef bgfx_handle_t bgfx_occlusion_query_handle_t;
typedef bgfx_handle_t bgfx_program_handle_t;
typedef bgfx_handle_t bgfx_shader_handle_t;
typedef bgfx_handle_t bgfx_texture_handle_t;
typedef bgfx_handle_t bgfx_uniform_handle_t;
typedef bgfx_handle_t bgfx_vertex_buffer_handle_t;
typedef bgfx_handle_t bgfx_data_memory_t;

// Vertex layout type (opaque struct)
typedef struct bgfx_vertex_layout {
    uint8_t padding[168]; // sizeof(bgfx::VertexLayout) on x64
} bgfx_vertex_layout_t;

// Renderer types - numeric values MUST match bgfx::RendererType::Enum in bgfx.h
typedef enum bgfx_renderer_type {
    BGFX_RENDERER_TYPE_NOOP = 0,
    BGFX_RENDERER_TYPE_AGC = 1,
    BGFX_RENDERER_TYPE_DIRECT3D11 = 2,
    BGFX_RENDERER_TYPE_DIRECT3D12 = 3,
    BGFX_RENDERER_TYPE_GNM = 4,
    BGFX_RENDERER_TYPE_METAL = 5,
    BGFX_RENDERER_TYPE_NVN = 6,
    BGFX_RENDERER_TYPE_OPENGLES = 7,
    BGFX_RENDERER_TYPE_OPENGL = 8,
    BGFX_RENDERER_TYPE_VULKAN = 9,
    BGFX_RENDERER_TYPE_WEBGPU = 10,
    BGFX_RENDERER_TYPE_COUNT = 11
} bgfx_renderer_type_t;

// Reset flags
#define BGFX_RESET_NONE                   0x00000000
#define BGFX_RESET_FULLSCREEN             0x00000001
#define BGFX_RESET_MSAA_X2                0x00000010
#define BGFX_RESET_MSAA_X4                0x00000020
#define BGFX_RESET_MSAA_X8                0x00000040
#define BGFX_RESET_MSAA_X16               0x00000080
#define BGFX_RESET_VSYNC                  0x00000800
#define BGFX_RESET_MAXANISOTROPY          0x00001000
#define BGFX_RESET_CAPTURE                0x00002000
#define BGFX_RESET_FLUSH_AFTER_RENDER     0x00008000
#define BGFX_RESET_FLIP_AFTER_RENDER      0x00040000
#define BGFX_RESET_SRGB_BACKBUFFER        0x00080000

// Clear flags
#define BGFX_CLEAR_NONE                   0x00000000
#define BGFX_CLEAR_COLOR                  0x00000001
#define BGFX_CLEAR_DEPTH                  0x00000002
#define BGFX_CLEAR_STENCIL                0x00000004

// Frame flags
#define BGFX_FRAME_NONE                   0x00000000
#define BGFX_FRAME_DEBUG_CAPTURE          0x00000001
#define BGFX_FRAME_DISCARD                0x00000002
#define BGFX_FRAME_FLUSH                  0x00000004

// Texture formats
typedef enum bgfx_texture_format {
    BGFX_TEXTURE_FORMAT_BC1 = 0,
    BGFX_TEXTURE_FORMAT_BC2 = 1,
    BGFX_TEXTURE_FORMAT_BC3 = 2,
    BGFX_TEXTURE_FORMAT_BC4 = 3,
    BGFX_TEXTURE_FORMAT_BC4S = 4,
    BGFX_TEXTURE_FORMAT_BC5 = 5,
    BGFX_TEXTURE_FORMAT_BC5S = 6,
    BGFX_TEXTURE_FORMAT_BC6H = 7,
    BGFX_TEXTURE_FORMAT_BC6HU = 8,
    BGFX_TEXTURE_FORMAT_BC7 = 9,
    BGFX_TEXTURE_FORMAT_ETC1 = 10,
    BGFX_TEXTURE_FORMAT_ETC2 = 11,
    BGFX_TEXTURE_FORMAT_ETC2A = 12,
    BGFX_TEXTURE_FORMAT_ETC2A1 = 13,
    BGFX_TEXTURE_FORMAT_EACR11 = 14,
    BGFX_TEXTURE_FORMAT_EACR11S = 15,
    BGFX_TEXTURE_FORMAT_EACRG11 = 16,
    BGFX_TEXTURE_FORMAT_EACRG11S = 17,
    BGFX_TEXTURE_FORMAT_PTC12 = 18,
    BGFX_TEXTURE_FORMAT_PTC14 = 19,
    BGFX_TEXTURE_FORMAT_PTC12A = 20,
    BGFX_TEXTURE_FORMAT_PTC14A = 21,
    BGFX_TEXTURE_FORMAT_PTC22 = 22,
    BGFX_TEXTURE_FORMAT_PTC24 = 23,
    BGFX_TEXTURE_FORMAT_ATC = 24,
    BGFX_TEXTURE_FORMAT_ATCE = 25,
    BGFX_TEXTURE_FORMAT_ATCI = 26,
    BGFX_TEXTURE_FORMAT_ASTC4X4 = 27,
    BGFX_TEXTURE_FORMAT_ASTC5X4 = 28,
    BGFX_TEXTURE_FORMAT_ASTC5X5 = 29,
    BGFX_TEXTURE_FORMAT_ASTC6X5 = 30,
    BGFX_TEXTURE_FORMAT_ASTC6X6 = 31,
    BGFX_TEXTURE_FORMAT_ASTC8X5 = 32,
    BGFX_TEXTURE_FORMAT_ASTC8X6 = 33,
    BGFX_TEXTURE_FORMAT_ASTC8X8 = 34,
    BGFX_TEXTURE_FORMAT_ASTC10X5 = 35,
    BGFX_TEXTURE_FORMAT_ASTC10X6 = 36,
    BGFX_TEXTURE_FORMAT_ASTC10X8 = 37,
    BGFX_TEXTURE_FORMAT_ASTC10X10 = 38,
    BGFX_TEXTURE_FORMAT_ASTC12X10 = 39,
    BGFX_TEXTURE_FORMAT_ASTC12X12 = 40,
    BGFX_TEXTURE_FORMAT_UNKNOWN = 41,
    BGFX_TEXTURE_FORMAT_R1 = 42,
    BGFX_TEXTURE_FORMAT_A8 = 43,
    BGFX_TEXTURE_FORMAT_R8 = 44,
    BGFX_TEXTURE_FORMAT_R8I = 45,
    BGFX_TEXTURE_FORMAT_R8U = 46,
    BGFX_TEXTURE_FORMAT_R8S = 47,
    BGFX_TEXTURE_FORMAT_R16 = 48,
    BGFX_TEXTURE_FORMAT_R16I = 49,
    BGFX_TEXTURE_FORMAT_R16U = 50,
    BGFX_TEXTURE_FORMAT_R16F = 51,
    BGFX_TEXTURE_FORMAT_R16S = 52,
    BGFX_TEXTURE_FORMAT_R32I = 53,
    BGFX_TEXTURE_FORMAT_R32U = 54,
    BGFX_TEXTURE_FORMAT_R32F = 55,
    BGFX_TEXTURE_FORMAT_RG8 = 56,
    BGFX_TEXTURE_FORMAT_RG8I = 57,
    BGFX_TEXTURE_FORMAT_RG8U = 58,
    BGFX_TEXTURE_FORMAT_RG8S = 59,
    BGFX_TEXTURE_FORMAT_RG16 = 60,
    BGFX_TEXTURE_FORMAT_RG16I = 61,
    BGFX_TEXTURE_FORMAT_RG16U = 62,
    BGFX_TEXTURE_FORMAT_RG16F = 63,
    BGFX_TEXTURE_FORMAT_RG16S = 64,
    BGFX_TEXTURE_FORMAT_RG32I = 65,
    BGFX_TEXTURE_FORMAT_RG32U = 66,
    BGFX_TEXTURE_FORMAT_RG32F = 67,
    BGFX_TEXTURE_FORMAT_RGB8 = 68,
    BGFX_TEXTURE_FORMAT_RGB8I = 69,
    BGFX_TEXTURE_FORMAT_RGB8U = 70,
    BGFX_TEXTURE_FORMAT_RGB8S = 71,
    BGFX_TEXTURE_FORMAT_RGB9E5F = 72,
    BGFX_TEXTURE_FORMAT_BGRA8 = 73,
    BGFX_TEXTURE_FORMAT_RGBA8 = 74,
    BGFX_TEXTURE_FORMAT_RGBA8I = 75,
    BGFX_TEXTURE_FORMAT_RGBA8U = 76,
    BGFX_TEXTURE_FORMAT_RGBA8S = 77,
    BGFX_TEXTURE_FORMAT_RGBA16 = 78,
    BGFX_TEXTURE_FORMAT_RGBA16I = 79,
    BGFX_TEXTURE_FORMAT_RGBA16U = 80,
    BGFX_TEXTURE_FORMAT_RGBA16F = 81,
    BGFX_TEXTURE_FORMAT_RGBA16S = 82,
    BGFX_TEXTURE_FORMAT_RGBA32I = 83,
    BGFX_TEXTURE_FORMAT_RGBA32U = 84,
    BGFX_TEXTURE_FORMAT_RGBA32F = 85,
    BGFX_TEXTURE_FORMAT_B5G6R5 = 86,
    BGFX_TEXTURE_FORMAT_R5G6B5 = 87,
    BGFX_TEXTURE_FORMAT_BGRA4 = 88,
    BGFX_TEXTURE_FORMAT_RGBA4 = 89,
    BGFX_TEXTURE_FORMAT_BGR5A1 = 90,
    BGFX_TEXTURE_FORMAT_RGB5A1 = 91,
    BGFX_TEXTURE_FORMAT_RGB10A2 = 92,
    BGFX_TEXTURE_FORMAT_RGB10A2U = 93,
    BGFX_TEXTURE_FORMAT_RG11B10F = 94,
    BGFX_TEXTURE_FORMAT_UNKNOWN_DEPTH = 95,
    BGFX_TEXTURE_FORMAT_D16 = 96,
    BGFX_TEXTURE_FORMAT_D24 = 97,
    BGFX_TEXTURE_FORMAT_D24S8 = 98,
    BGFX_TEXTURE_FORMAT_D32 = 99,
    BGFX_TEXTURE_FORMAT_D16F = 100,
    BGFX_TEXTURE_FORMAT_D24F = 101,
    BGFX_TEXTURE_FORMAT_D32F = 102,
    BGFX_TEXTURE_FORMAT_D32FS8 = 103,
    BGFX_TEXTURE_FORMAT_D0S8 = 104,
    BGFX_TEXTURE_FORMAT_COUNT = 105
} bgfx_texture_format_t;

// Platform data
typedef struct bgfx_platform_data {
    void* ndt;
    void* nwh;
    void* context;
    void* queue;
    void* backBuffer;
    void* backBufferDS;
    uint8_t type;
} bgfx_platform_data_t;

// Resolution
typedef struct bgfx_resolution {
    bgfx_texture_format_t formatColor;
    bgfx_texture_format_t formatDepthStencil;
    uint32_t width;
    uint32_t height;
    uint32_t reset;
    uint8_t numBackBuffers;
    uint8_t maxFrameLatency;
    uint8_t debugTextScale;
} bgfx_resolution_t;

// Init limits
typedef struct bgfx_init_limits {
    uint16_t maxEncoders;
    uint32_t numDrawCalls;
    uint32_t numDrawCallPeakFrames;
    uint32_t minResourceCbSize;
    uint32_t maxTransientVbSize;
    uint32_t maxTransientIbSize;
    uint32_t minUniformBufferSize;
} bgfx_init_limits_t;

// Init structure
typedef struct bgfx_init {
    bgfx_renderer_type_t type;
    uint16_t vendorId;
    uint16_t deviceId;
    uint64_t capabilities;
    bool debug;
    bool profile;
    bool fallback;
    bool videoDecode;
    bgfx_platform_data_t platformData;
    bgfx_resolution_t resolution;
    bgfx_init_limits_t limits;
    void* callback;
    void* allocator;
} bgfx_init_t;

// Caps structures (simplified)
typedef struct bgfx_caps_gpu {
    uint16_t vendorId;
    uint16_t deviceId;
} bgfx_caps_gpu_t;

typedef struct bgfx_caps_limits {
    uint32_t maxDrawCalls;
    uint32_t maxBlits;
    uint32_t maxTextureSize;
    uint32_t maxTextureLayers;
    uint32_t maxViews;
    uint32_t maxFrameBuffers;
    uint32_t maxFBAttachments;
    uint32_t maxPrograms;
    uint32_t maxShaders;
    uint32_t maxTextures;
    uint32_t maxTextureSamplers;
    uint32_t maxComputeBindings;
    uint32_t maxVertexLayouts;
    uint32_t maxVertexStreams;
    uint32_t maxIndexBuffers;
    uint32_t maxVertexBuffers;
    uint32_t maxDynamicIndexBuffers;
    uint32_t maxDynamicVertexBuffers;
    uint32_t maxUniforms;
    uint32_t maxOcclusionQueries;
    uint32_t maxEncoders;
    uint32_t minResourceCbSize;
    uint32_t maxTransientVbSize;
    uint32_t maxTransientIbSize;
} bgfx_caps_limits_t;

typedef struct bgfx_caps {
    bgfx_renderer_type_t rendererType;
    uint64_t supported;
    uint16_t vendorId;
    uint16_t deviceId;
    bool homogeneousDepth;
    bool originBottomLeft;
    uint8_t numGPUs;
    bgfx_caps_gpu_t gpu[4];
    bgfx_caps_limits_t limits;
    uint16_t formats[87];
} bgfx_caps_t;

// State flags (must match bgfx defines.h exactly)
#define BGFX_STATE_WRITE_R                    UINT64_C(0x0000000000000001)
#define BGFX_STATE_WRITE_G                    UINT64_C(0x0000000000000002)
#define BGFX_STATE_WRITE_B                    UINT64_C(0x0000000000000004)
#define BGFX_STATE_WRITE_A                    UINT64_C(0x0000000000000008)
#define BGFX_STATE_WRITE_Z                    UINT64_C(0x0000004000000000)
#define BGFX_STATE_WRITE_RGB                  (BGFX_STATE_WRITE_R|BGFX_STATE_WRITE_G|BGFX_STATE_WRITE_B)
#define BGFX_STATE_WRITE_MASK                 (BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z)

#define BGFX_STATE_DEPTH_TEST_LESS            UINT64_C(0x0000000000000010)
#define BGFX_STATE_DEPTH_TEST_LEQUAL          UINT64_C(0x0000000000000020)
#define BGFX_STATE_DEPTH_TEST_EQUAL           UINT64_C(0x0000000000000030)
#define BGFX_STATE_DEPTH_TEST_GEQUAL          UINT64_C(0x0000000000000040)
#define BGFX_STATE_DEPTH_TEST_GREATER         UINT64_C(0x0000000000000050)
#define BGFX_STATE_DEPTH_TEST_NOTEQUAL        UINT64_C(0x0000000000000060)
#define BGFX_STATE_DEPTH_TEST_NEVER           UINT64_C(0x0000000000000070)
#define BGFX_STATE_DEPTH_TEST_ALWAYS          UINT64_C(0x0000000000000080)
#define BGFX_STATE_DEPTH_TEST_SHIFT           4
#define BGFX_STATE_DEPTH_TEST_MASK            UINT64_C(0x00000000000000f0)

#define BGFX_STATE_BLEND_ZERO                 UINT64_C(0x0000000000001000)
#define BGFX_STATE_BLEND_ONE                  UINT64_C(0x0000000000002000)
#define BGFX_STATE_BLEND_SRC_COLOR            UINT64_C(0x0000000000003000)
#define BGFX_STATE_BLEND_INV_SRC_COLOR        UINT64_C(0x0000000000004000)
#define BGFX_STATE_BLEND_SRC_ALPHA            UINT64_C(0x0000000000005000)
#define BGFX_STATE_BLEND_INV_SRC_ALPHA        UINT64_C(0x0000000000006000)
#define BGFX_STATE_BLEND_DST_ALPHA            UINT64_C(0x0000000000007000)
#define BGFX_STATE_BLEND_INV_DST_ALPHA        UINT64_C(0x0000000000008000)
#define BGFX_STATE_BLEND_DST_COLOR            UINT64_C(0x0000000000009000)
#define BGFX_STATE_BLEND_INV_DST_COLOR        UINT64_C(0x000000000000a000)
#define BGFX_STATE_BLEND_SRC_ALPHA_SAT        UINT64_C(0x000000000000b000)
#define BGFX_STATE_BLEND_FACTOR               UINT64_C(0x000000000000c000)
#define BGFX_STATE_BLEND_INV_FACTOR           UINT64_C(0x000000000000d000)
#define BGFX_STATE_BLEND_SHIFT                12
#define BGFX_STATE_BLEND_MASK                 UINT64_C(0x000000000ffff000)

#define BGFX_STATE_BLEND_EQUATION_ADD         UINT64_C(0x0000000000000000)
#define BGFX_STATE_BLEND_EQUATION_SUB         UINT64_C(0x0000000010000000)
#define BGFX_STATE_BLEND_EQUATION_REVSUB      UINT64_C(0x0000000020000000)
#define BGFX_STATE_BLEND_EQUATION_MIN         UINT64_C(0x0000000030000000)
#define BGFX_STATE_BLEND_EQUATION_MAX         UINT64_C(0x0000000040000000)
#define BGFX_STATE_BLEND_EQUATION_SHIFT       28
#define BGFX_STATE_BLEND_EQUATION_MASK        UINT64_C(0x00000003f0000000)

#define BGFX_STATE_CULL_CW                    UINT64_C(0x0000001000000000)
#define BGFX_STATE_CULL_CCW                   UINT64_C(0x0000002000000000)
#define BGFX_STATE_CULL_SHIFT                 36
#define BGFX_STATE_CULL_MASK                  UINT64_C(0x0000003000000000)

#define BGFX_STATE_ALPHA_REF_SHIFT            40
#define BGFX_STATE_ALPHA_REF_MASK             UINT64_C(0x0000ff0000000000)
#define BGFX_STATE_ALPHA_REF(v)               (((uint64_t)(v)<<BGFX_STATE_ALPHA_REF_SHIFT)&BGFX_STATE_ALPHA_REF_MASK)

#define BGFX_STATE_PT_TRISTRIP                UINT64_C(0x0001000000000000)
#define BGFX_STATE_PT_LINES                   UINT64_C(0x0002000000000000)
#define BGFX_STATE_PT_LINESTRIP               UINT64_C(0x0003000000000000)
#define BGFX_STATE_PT_POINTS                  UINT64_C(0x0004000000000000)
#define BGFX_STATE_PT_SHIFT                   48
#define BGFX_STATE_PT_MASK                    UINT64_C(0x0007000000000000)

#define BGFX_STATE_POINT_SIZE_SHIFT           52
#define BGFX_STATE_POINT_SIZE_MASK            UINT64_C(0x00f0000000000000)
#define BGFX_STATE_POINT_SIZE(v)              (((uint64_t)(v)<<BGFX_STATE_POINT_SIZE_SHIFT)&BGFX_STATE_POINT_SIZE_MASK)

#define BGFX_STATE_MSAA                       UINT64_C(0x0100000000000000)
#define BGFX_STATE_LINEAA                     UINT64_C(0x0200000000000000)
#define BGFX_STATE_CONSERVATIVE_RASTER        UINT64_C(0x0400000000000000)
#define BGFX_STATE_NONE                       UINT64_C(0x0000000000000000)
#define BGFX_STATE_FRONT_CCW                  UINT64_C(0x0000008000000000)
#define BGFX_STATE_BLEND_INDEPENDENT          UINT64_C(0x0000000400000000)
#define BGFX_STATE_BLEND_ALPHA_TO_COVERAGE    UINT64_C(0x0000000800000000)
#define BGFX_STATE_DEFAULT                    (BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|BGFX_STATE_CULL_CW|BGFX_STATE_MSAA)

// Blend functions
#define BGFX_STATE_BLEND_FUNC_SEPARATE(_srcRGB, _dstRGB, _srcA, _dstA) (UINT64_C(0) \
	| ( (uint64_t)(_srcRGB)|( (uint64_t)(_dstRGB)<<4) ) \
	| ( ( (uint64_t)(_srcA)|( (uint64_t)(_dstA)<<4) )<<8) \
	)
#define BGFX_STATE_BLEND_FUNC(_src, _dst)     BGFX_STATE_BLEND_FUNC_SEPARATE(_src, _dst, _src, _dst)
#define BGFX_STATE_BLEND_ALPHA                BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)

// Texture flags (combined with sampler flags in bgfx_create_texture_2d)
#define BGFX_TEXTURE_NONE                         UINT64_C(0x0000000000000000)
#define BGFX_TEXTURE_RT                           UINT64_C(0x0000001000000000)
#define BGFX_TEXTURE_U_CLAMP                      UINT64_C(0x0000000000000002)
#define BGFX_TEXTURE_V_CLAMP                      UINT64_C(0x0000000000000008)
#define BGFX_TEXTURE_MIN_POINT                    UINT64_C(0x0000000000000040)
#define BGFX_TEXTURE_MAG_POINT                    UINT64_C(0x0000000000000100)
#define BGFX_TEXTURE_MIP_POINT                    UINT64_C(0x0000000000000200)

// Sampler flags (used with bgfx_set_texture, etc.)
#define BGFX_SAMPLER_MIN_POINT                    UINT32_C(0x00000040)
#define BGFX_SAMPLER_MAG_POINT                    UINT32_C(0x00000100)
#define BGFX_SAMPLER_MIP_POINT                    UINT32_C(0x00000200)

// Discard flags
#define BGFX_DISCARD_NONE                     0x00
#define BGFX_DISCARD_ALL                      0xFF

// Vertex attributes
typedef enum bgfx_attrib {
    BGFX_ATTRIB_POSITION = 0,
    BGFX_ATTRIB_NORMAL,
    BGFX_ATTRIB_TANGENT,
    BGFX_ATTRIB_BITANGENT,
    BGFX_ATTRIB_COLOR0,
    BGFX_ATTRIB_COLOR1,
    BGFX_ATTRIB_COLOR2,
    BGFX_ATTRIB_COLOR3,
    BGFX_ATTRIB_INDICES,
    BGFX_ATTRIB_WEIGHT,
    BGFX_ATTRIB_TEXCOORD0,
    BGFX_ATTRIB_TEXCOORD1,
    BGFX_ATTRIB_TEXCOORD2,
    BGFX_ATTRIB_TEXCOORD3,
    BGFX_ATTRIB_TEXCOORD4,
    BGFX_ATTRIB_TEXCOORD5,
    BGFX_ATTRIB_TEXCOORD6,
    BGFX_ATTRIB_TEXCOORD7,
    BGFX_ATTRIB_COUNT
} bgfx_attrib_t;

typedef enum bgfx_attrib_type {
    BGFX_ATTRIB_TYPE_INT8 = 0,
    BGFX_ATTRIB_TYPE_UINT8,
    BGFX_ATTRIB_TYPE_UINT10,
    BGFX_ATTRIB_TYPE_INT16,
    BGFX_ATTRIB_TYPE_UINT16,
    BGFX_ATTRIB_TYPE_HALF,
    BGFX_ATTRIB_TYPE_FLOAT,
    BGFX_ATTRIB_TYPE_INT32,
    BGFX_ATTRIB_TYPE_UINT32,
    BGFX_ATTRIB_TYPE_COUNT
} bgfx_attrib_type_t;

// Memory block (returned by bgfx_alloc / bgfx_copy)
typedef struct bgfx_memory {
    void* data;
    uint32_t size;
} bgfx_memory_t;

// Uniform type
typedef enum bgfx_uniform_type {
    BGFX_UNIFORM_TYPE_SAMPLER = 0,
    BGFX_UNIFORM_TYPE_END,
    BGFX_UNIFORM_TYPE_VEC4,
    BGFX_UNIFORM_TYPE_MAT3,
    BGFX_UNIFORM_TYPE_MAT4,
    BGFX_UNIFORM_TYPE_COUNT
} bgfx_uniform_type_t;

typedef bgfx_handle_t bgfx_vertex_layout_handle_t;

typedef enum bgfx_view_mode {
    BGFX_VIEW_MODE_DEFAULT = 0,
    BGFX_VIEW_MODE_SEQUENTIAL,
    BGFX_VIEW_MODE_DEPTH_ASCENDING,
    BGFX_VIEW_MODE_DEPTH_DESCENDING,
} bgfx_view_mode_t;

// Transient vertex buffer
typedef struct bgfx_transient_vertex_buffer {
    void* data;
    uint32_t size;
    uint32_t start;
    uint16_t stride;
    bgfx_vertex_buffer_handle_t handle;
    bgfx_vertex_layout_handle_t layoutHandle;
} bgfx_transient_vertex_buffer_t;

typedef struct bgfx_transient_index_buffer {
    void* data;
    uint32_t size;
    uint32_t start;
    bgfx_index_buffer_handle_t handle;
    bool index32;
} bgfx_transient_index_buffer_t;

// View ID type
typedef uint16_t bgfx_view_id_t;

// Invalid handle
#define BGFX_INVALID_HANDLE { 0xFFFF }

// Encoder (use NULL for default encoder)
typedef struct bgfx_encoder_t bgfx_encoder_t;

// Core C API functions
void bgfx_init_ctor(bgfx_init_t* _init);
bool bgfx_init(const bgfx_init_t* _init);
void bgfx_shutdown(void);
void bgfx_reset(uint32_t _width, uint32_t _height, uint32_t _flags, bgfx_texture_format_t _format);
uint32_t bgfx_frame(uint8_t _flags);
bgfx_renderer_type_t bgfx_get_renderer_type(void);
const char* bgfx_get_renderer_name(bgfx_renderer_type_t _type);
const bgfx_caps_t* bgfx_get_caps(void);

// View functions
void bgfx_set_view_name(uint16_t _id, const char* _name, int32_t _len);
void bgfx_set_view_rect(uint16_t _id, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height);
void bgfx_set_view_clear(uint16_t _id, uint16_t _flags, uint32_t _rgba, float _depth, uint8_t _stencil);
void bgfx_set_view_mode(uint16_t _id, bgfx_view_mode_t _mode);
void bgfx_set_view_transform(uint16_t _id, const void* _view, const void* _proj);
void bgfx_set_view_order(uint16_t _id, uint16_t _num, const bgfx_view_id_t* _order);
void bgfx_touch(uint16_t _id);
void bgfx_reset_view(uint16_t _id);

// Vertex layout functions
bgfx_vertex_layout_t* bgfx_vertex_layout_begin(bgfx_vertex_layout_t* _this, bgfx_renderer_type_t _rendererType);
bgfx_vertex_layout_t* bgfx_vertex_layout_add(bgfx_vertex_layout_t* _this, bgfx_attrib_t _attrib, uint8_t _num, bgfx_attrib_type_t _type, bool _normalized, bool _asInt);
void bgfx_vertex_layout_decode(const bgfx_vertex_layout_t* _this, bgfx_attrib_t _attrib, uint8_t* _num, bgfx_attrib_type_t* _type, bool* _normalized, bool* _asInt);
bool bgfx_vertex_layout_has(const bgfx_vertex_layout_t* _this, bgfx_attrib_t _attrib);
bgfx_vertex_layout_t* bgfx_vertex_layout_skip(bgfx_vertex_layout_t* _this, uint8_t _num);
void bgfx_vertex_layout_end(bgfx_vertex_layout_t* _this);
uint16_t bgfx_vertex_layout_get_offset(const bgfx_vertex_layout_t* _this, bgfx_attrib_t _attrib);
uint16_t bgfx_vertex_layout_get_stride(const bgfx_vertex_layout_t* _this);
uint32_t bgfx_vertex_layout_get_size(const bgfx_vertex_layout_t* _this, uint32_t _num);
bgfx_vertex_layout_handle_t bgfx_create_vertex_layout(const bgfx_vertex_layout_t* _layout);
void bgfx_destroy_vertex_layout(bgfx_vertex_layout_handle_t _layoutHandle);

// Static buffer functions (implemented in bgfx.idl.inl; exposed here for the
// world-render pass that uploads level geometry once).
bgfx_vertex_buffer_handle_t bgfx_create_vertex_buffer(const bgfx_memory_t* _mem, const bgfx_vertex_layout_t* _layout, uint16_t _flags);
void bgfx_destroy_vertex_buffer(bgfx_vertex_buffer_handle_t _handle);
bgfx_index_buffer_handle_t bgfx_create_index_buffer(const bgfx_memory_t* _mem, uint16_t _flags);
void bgfx_destroy_index_buffer(bgfx_index_buffer_handle_t _handle);

// Transient buffer functions
uint32_t bgfx_get_avail_transient_vertex_buffer(uint32_t _num, const bgfx_vertex_layout_t* _layout);
void bgfx_alloc_transient_vertex_buffer(bgfx_transient_vertex_buffer_t* _tvb, uint32_t _num, const bgfx_vertex_layout_t* _layout);
void bgfx_alloc_transient_index_buffer(bgfx_transient_index_buffer_t* _tib, uint32_t _num, bool _index32);
bool bgfx_alloc_transient_buffers(bgfx_transient_vertex_buffer_t* _tvb, const bgfx_vertex_layout_t* _layout, uint32_t _numVertices, bgfx_transient_index_buffer_t* _tib, uint32_t _numIndices, bool _index32);

// Encoder functions
bgfx_encoder_t* bgfx_encoder_begin(bool _forThread);
void bgfx_encoder_end(bgfx_encoder_t* _this);
void bgfx_encoder_set_state(bgfx_encoder_t* _this, uint64_t _state, uint32_t _rgba);
void bgfx_encoder_set_transient_vertex_buffer(bgfx_encoder_t* _this, uint8_t _stream, const bgfx_transient_vertex_buffer_t* _tvb, uint32_t _startVertex, uint32_t _numVertices);
void bgfx_encoder_set_transient_vertex_buffer_with_layout(bgfx_encoder_t* _this, uint8_t _stream, const bgfx_transient_vertex_buffer_t* _tvb, uint32_t _startVertex, uint32_t _numVertices, bgfx_vertex_layout_handle_t _layoutHandle);
void bgfx_encoder_set_index_buffer(bgfx_encoder_t* _this, bgfx_index_buffer_handle_t _handle, uint32_t _first, uint32_t _num);
void bgfx_encoder_set_transient_index_buffer(bgfx_encoder_t* _this, const bgfx_transient_index_buffer_t* _tib, uint32_t _startIndex, uint32_t _numIndices);
void bgfx_encoder_set_texture(bgfx_encoder_t* _this, uint8_t _stage, bgfx_uniform_handle_t _sampler, bgfx_texture_handle_t _handle, uint32_t _flags);
void bgfx_encoder_submit(bgfx_encoder_t* _this, bgfx_view_id_t _id, bgfx_program_handle_t _program, uint32_t _depth, uint8_t _flags);

// Immediate-mode static buffer binding
void bgfx_set_vertex_buffer_with_layout(uint8_t _stream, bgfx_vertex_buffer_handle_t _handle, uint32_t _startVertex, uint32_t _numVertices, bgfx_vertex_layout_handle_t _layoutHandle);
void bgfx_set_vertex_buffer(uint8_t _stream, bgfx_vertex_buffer_handle_t _handle, uint32_t _startVertex, uint32_t _numVertices);
void bgfx_set_index_buffer(bgfx_index_buffer_handle_t _handle, uint32_t _firstIndex, uint32_t _numIndices);

// Shader/program functions
bgfx_shader_handle_t bgfx_create_shader(const bgfx_memory_t* _mem);
bgfx_uniform_handle_t bgfx_create_uniform(const char* _name, bgfx_uniform_type_t _type, uint16_t _num);
bgfx_program_handle_t bgfx_create_program(bgfx_shader_handle_t _vsh, bgfx_shader_handle_t _fsh, bool _destroyShaders);
void bgfx_destroy_shader(bgfx_shader_handle_t _handle);
void bgfx_destroy_uniform(bgfx_uniform_handle_t _handle);
void bgfx_destroy_program(bgfx_program_handle_t _handle);

// Memory allocation
const bgfx_memory_t* bgfx_alloc(uint32_t _size);
const bgfx_memory_t* bgfx_make_ref(const void* _data, uint32_t _size);
const bgfx_memory_t* bgfx_copy(const void* _data, uint32_t _size);

uint16_t bgfx_set_scissor(uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height);
void bgfx_set_scissor_cached(uint16_t _cache);

void bgfx_request_screen_shot(bgfx_frame_buffer_handle_t _handle, const char* _filePath);

// Immediate mode (default encoder) functions
void bgfx_set_state(uint64_t _state, uint32_t _rgba);
uint32_t bgfx_set_transform(const void* _mtx, uint16_t _num);
void bgfx_set_transient_vertex_buffer(uint8_t _stream, const bgfx_transient_vertex_buffer_t* _tvb, uint32_t _startVertex, uint32_t _numVertices);
void bgfx_set_transient_index_buffer(const bgfx_transient_index_buffer_t* _tib, uint32_t _startIndex, uint32_t _numIndices);
void bgfx_set_texture(uint8_t _stage, bgfx_uniform_handle_t _sampler, bgfx_texture_handle_t _handle, uint32_t _flags);
void bgfx_set_uniform(bgfx_uniform_handle_t _handle, const void* _value, uint16_t _num);
void bgfx_submit(bgfx_view_id_t _id, bgfx_program_handle_t _program, uint32_t _depth, uint8_t _flags);

// Texture functions
bool bgfx_is_texture_valid(uint16_t _depth, bool _cubeMap, uint16_t _numLayers, bgfx_texture_format_t _format, uint64_t _flags);
bgfx_texture_handle_t bgfx_create_texture_2d(uint16_t _width, uint16_t _height, bool _hasMips, uint16_t _numLayers, bgfx_texture_format_t _format, uint64_t _flags, const bgfx_memory_t* _mem, uint64_t _external);
void bgfx_destroy_texture(bgfx_texture_handle_t _handle);
void bgfx_update_texture_2d(bgfx_texture_handle_t _handle, uint16_t _layer, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height, const bgfx_memory_t* _mem, uint16_t _pitch);

// Framebuffer functions
bgfx_frame_buffer_handle_t bgfx_create_frame_buffer_from_handles(uint8_t _num, const bgfx_texture_handle_t* _handles, bool _destroyTexture);
void bgfx_destroy_frame_buffer(bgfx_frame_buffer_handle_t _handle);
void bgfx_set_view_frame_buffer(bgfx_view_id_t _id, bgfx_frame_buffer_handle_t _handle);

static bool bgfxIsValid(bgfx_handle_t _handle) { return _handle.idx != UINT16_MAX; }

#ifdef __cplusplus
}
#endif
