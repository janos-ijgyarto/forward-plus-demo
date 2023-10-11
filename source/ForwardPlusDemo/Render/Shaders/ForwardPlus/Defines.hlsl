#include "../Defines.hlsl"

// Limit to 128 to avoid "overfilling" the GPU with a single thread group
#ifndef MAX_CS_THREAD_COUNT
#define MAX_CS_THREAD_COUNT 128
#endif

#define POINT_LIGHT_STRIDE 4
#define SPOT_LIGHT_MAX_TRIANGLES 8
#define SPOT_LIGHT_STRIDE (SPOT_LIGHT_MAX_TRIANGLES * 4)

struct SpotLightCullingData
{
    float4 projected_points[5];
    float4 z_parameters;
};

#define SPOT_LIGHT_CULLING_DATA_STRIDE 6

cbuffer ForwardPlusCSConstants : register(b1)
{
    struct
    {
        float4 camera_pos;
        float4 camera_front;
        
        float4 clip_scale;
        
        float4x4 view;
        float4x4 view_projection;
    } ForwardPlusCSConstants;
};

StructuredBuffer<LightInfo> LightInfoBuffer : register(t0);