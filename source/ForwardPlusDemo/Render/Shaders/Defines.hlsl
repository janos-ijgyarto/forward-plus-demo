#define Z_BIN_MIN_MASK ((1 << 16) - 1)

#ifndef TILE_X_DIM
#define TILE_X_DIM 32
#endif

#ifndef TILE_Y_DIM
#define TILE_Y_DIM 24
#endif

#ifndef Z_BIN_COUNT
#define Z_BIN_COUNT 1024
#endif

#define LIGHT_BATCH_SIZE 32

#define LIGHT_TYPE_POINT 0
#define LIGHT_TYPE_DIRECTIONAL 1
#define LIGHT_TYPE_SPOT 2

struct LightInfo
{
    uint type; // Point, directional, or spot
    uint index; // Index of light within its type (used by CS stages)
    uint z_range; // Indices of min and max Z bin spanned by this light (using the lower and upper 16 bits respectively)
    uint _padding;
};

struct LightData
{
    float3 position;
    float inv_range;

    float3 direction;
    float cos_outer_angle;

    float3 diffuse;
    float inv_cos_inner_angle;

    float3 ambient;
    float linear_attenuation;
    
    LightInfo info; // Stores metadata (light type, etc.)
};

struct ZBin
{
    uint min;
    uint max;
};

ZBin read_z_bin(uint z_bin_data)
{    
    ZBin z_bin;
    z_bin.min = (z_bin_data & Z_BIN_MIN_MASK);
    z_bin.max = (z_bin_data >> 16);
    
    return z_bin;
}

bool is_z_bin_valid(ZBin z_bin)
{
    return (z_bin.min <= z_bin.max);
}

cbuffer ForwardPlusParameters : register(b0)
{
    struct
    {
        LightData global_light;

        uint4 light_counts; // In same order as light types

        float z_near;
        float z_far;

        uint2 resolution;
    } ForwardPlusParameters;
};

uint get_light_type_count(uint light_type)
{
    return ForwardPlusParameters.light_counts[light_type];
}

uint get_total_light_count()
{
    return ForwardPlusParameters.light_counts.x + ForwardPlusParameters.light_counts.y + ForwardPlusParameters.light_counts.z;
}

bool have_visible_lights()
{
    return any(ForwardPlusParameters.light_counts);
}

uint integer_division_ceil(uint numerator, uint denominator)
{
    return (numerator + (denominator - 1)) / denominator;
}

struct LightCullingDataIndex
{
    uint2 tile_index;
    uint z_bin;
};

uint find_z_bin(float view_z)
{
    const float z_offset = view_z - ForwardPlusParameters.z_near;
    const float z_distance = ForwardPlusParameters.z_far - ForwardPlusParameters.z_near;
    const float z_step = z_distance / Z_BIN_COUNT;

    return clamp(uint(z_offset / z_step), 0, Z_BIN_COUNT - 1);
}

LightCullingDataIndex get_light_culling_data_index(float2 pixel_pos, float view_z)
{
    LightCullingDataIndex data_index;
	
    data_index.z_bin = find_z_bin(view_z);
	
    // For the tiles, we have to work from the bottom left corner, as this is what the culling stage assumes
    // We get the indices from top left, then remap to bottom left
    const float2 tile_step = float2(ForwardPlusParameters.resolution) / float2(TILE_X_DIM, TILE_Y_DIM);
    const uint2 top_left_indices = clamp(uint2(pixel_pos / tile_step), uint2(0, 0), uint2(TILE_X_DIM - 1, TILE_Y_DIM - 1));
    
    data_index.tile_index.x = top_left_indices.x;
    data_index.tile_index.y = (TILE_Y_DIM - 1) - top_left_indices.y;
	
    return data_index;
}