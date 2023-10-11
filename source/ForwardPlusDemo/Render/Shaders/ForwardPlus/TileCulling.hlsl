#include "Defines.hlsl"

#ifndef LIGHTS_PER_GROUP
#define LIGHTS_PER_GROUP LIGHT_BATCH_SIZE
#endif

#ifndef TILES_PER_GROUP
#define TILES_PER_GROUP 4
#endif

struct PointLightData
{
    float4 ranges;
    float4 transformed_ranges;
    float2x2 clip_transform;
    float4 ellipse_params;
};

StructuredBuffer<float4> TileCullingData : register(t1);
RWStructuredBuffer<uint> TileBitmasks : register(u0);

uint get_tile_flat_index(uint3 group_id, uint3 group_thread_id)
{   
    const uint tile_base_offset = group_id.y * TILES_PER_GROUP;
    return tile_base_offset + group_thread_id.y;
}

uint2 get_tile_indices(uint3 group_id, uint3 group_thread_id)
{
    uint2 tile_indices;
    const uint tile_flat_index = get_tile_flat_index(group_id, group_thread_id);
    
    tile_indices.x = tile_flat_index % TILE_X_DIM;
    tile_indices.y = tile_flat_index / TILE_X_DIM;
    
    return tile_indices;
}

uint get_tile_bitmask_offset(uint3 group_id, uint3 group_thread_id)
{
    const uint tile_flat_index = get_tile_flat_index(group_id, group_thread_id);
    const uint bitmask_count = integer_division_ceil(get_total_light_count(), LIGHTS_PER_GROUP);
    
    // Go to the start offset for this tile, offset further by which group of lights is currently processed
    return (tile_flat_index * bitmask_count) + group_id.x;
}

uint get_light_index(uint3 group_id, uint3 group_thread_id)
{    
    const uint light_base_offset = group_id.x * LIGHTS_PER_GROUP;
    return light_base_offset + group_thread_id.x;
}

PointLightData get_point_light_data(LightInfo light_info)
{
    const uint point_light_data_offset = light_info.index * POINT_LIGHT_STRIDE;
    
    PointLightData data; 
    data.ranges = TileCullingData[point_light_data_offset];
    data.transformed_ranges = TileCullingData[point_light_data_offset + 1];
    
    const float4 clip_transform_vec = TileCullingData[point_light_data_offset + 2];
    data.clip_transform = float2x2(clip_transform_vec);
    
    data.ellipse_params = TileCullingData[point_light_data_offset + 3];
    
    return data;
}

float4x4 get_spot_light_triangle(uint spot_data_offset, uint triangle_index)
{
    float4x4 triangle_data;
    const uint triangle_offset = spot_data_offset + (triangle_index * 4);
    for (uint row_index = 0; row_index < 4; ++row_index)
    {
        const float4 current_data = TileCullingData[triangle_offset + row_index];
        triangle_data[row_index] = current_data;
    }
    
    return triangle_data;
}

bool test_point_light(float2 uv, float2 uv_stride, LightInfo light_info)
{
    // FIXME: still have a tiny edge case where the near plane touching the light sphere can cause the tile culling to fail (might be float precision or some other issue?)
    bool result;
    const PointLightData light_data = get_point_light_data(light_info);

    if (light_data.ellipse_params.x != 0.0f)
    {
        // Valid ellipse, perform more granular culling
        const float2 intersection_center = 0.5f * (light_data.transformed_ranges.xz + light_data.transformed_ranges.yw);

        // Get the tile coordinates in the projected space
        float2 clip_lo = uv;
        float2 clip_hi = uv + uv_stride;
        clip_lo *= ForwardPlusCSConstants.clip_scale.zw;
        clip_hi *= ForwardPlusCSConstants.clip_scale.zw;

        // For each corner of the tile, transform them into "ellipse space" and get their distance vector from the center
        float2 dist_00 = mul(light_data.clip_transform, float2(clip_lo.x, clip_lo.y)) - intersection_center;
        float2 dist_01 = mul(light_data.clip_transform, float2(clip_lo.x, clip_hi.y)) - intersection_center;
        float2 dist_10 = mul(light_data.clip_transform, float2(clip_hi.x, clip_lo.y)) - intersection_center;
        float2 dist_11 = mul(light_data.clip_transform, float2(clip_hi.x, clip_hi.y)) - intersection_center;

        // Multiply these distance vectors with the inverse radius (i.e normalize w.r.t the ellipse)
        dist_00 *= light_data.ellipse_params.yz;
        dist_01 *= light_data.ellipse_params.yz;
        dist_10 *= light_data.ellipse_params.yz;
        dist_11 *= light_data.ellipse_params.yz;

        // Check the maximum available distance
        float max_diag = max(distance(dist_00, dist_11), distance(dist_01, dist_10));
        float min_sq_dist = 1.0 + max_diag;
        min_sq_dist *= min_sq_dist;

        // Make sure all points are within the ellipse
        // FIXME: this seems very conservative, might be better to just check if the tile intersects the ellipse at all?
        // Otherwise we'll need much higher tile resolution
        float4 d = float4(dot(dist_00, dist_00), dot(dist_01, dist_01), dot(dist_10, dist_10), dot(dist_11, dist_11));
        result = all(d < min_sq_dist);
    }
    else
    {
        // Just check whether the tile is entirely within the light boundaries
        result = all(bool4((uv + uv_stride) > light_data.ranges.xy, (uv < light_data.ranges.zw)));
    }

    return result;
}

bool test_spot_light(float2 uv, float2 uv_stride, LightInfo light_info)
{
    bool result;
    
    const uint spot_light_data_offset = (get_light_type_count(LIGHT_TYPE_POINT) * POINT_LIGHT_STRIDE) + (light_info.index * SPOT_LIGHT_STRIDE);
    
    const float4x4 first_triangle = get_spot_light_triangle(spot_light_data_offset, 0);
    
    const uint num_triangles = asuint(first_triangle[0].w);
    if (num_triangles <= SPOT_LIGHT_MAX_TRIANGLES)
    {
        result = false;
        for (uint triangle_index = 0; triangle_index < num_triangles; ++triangle_index)
        {
            // First check if we are even in the triangle bounding box
            const float4x4 current_triangle = get_spot_light_triangle(spot_light_data_offset, triangle_index);
            const float4 screen_bb = current_triangle[3];
            if (all(bool4(((uv + uv_stride) > screen_bb.xy), (uv < screen_bb.zw))))
            {
                // Test against the actual triangle
                float3 base = current_triangle[0].xyz;
                const float3 dx = current_triangle[1].xyz;
                const float3 dy = current_triangle[2].xyz;

                base += dx * uv.x;
                base += dy * uv.y;
                base += lerp(float3(0.0f, 0.0f, 0.0f), uv_stride.x * dx, (dx > float3(0.0f, 0.0f, 0.0f)));
                base += lerp(float3(0.0f, 0.0f, 0.0f), uv_stride.y * dy, (dy > float3(0.0f, 0.0f, 0.0f)));
                if (all(base > float3(0.0f, 0.0f, 0.0f)))
                {
                    // At least one triangle fits, so we can say the tile is affected
                    result = true;
                    break;
                }
            }
        }
    }
    else
    {
        result = true;
    }

    return result;
}

groupshared uint SharedData[LIGHTS_PER_GROUP * TILES_PER_GROUP];

[numthreads(LIGHTS_PER_GROUP, TILES_PER_GROUP, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
    const uint light_index = get_light_index(group_id, group_thread_id);
    
    // Start by setting shared data to 0 (needed for threads that do nothing)
    SharedData[group_index] = 0;
    
    if (light_index < get_total_light_count())
    {    
        const uint2 tile_indices = get_tile_indices(group_id, group_thread_id);
        const LightInfo light_info = LightInfoBuffer[light_index];
    
        // Get the coordinates in screen space
        const float2 inv_resolution = 1.0f / float2(TILE_X_DIM, TILE_Y_DIM);
        const float2 uv = 2.0f * float2(tile_indices) * inv_resolution - 1.0f;
        const float2 uv_stride = 2.0f * inv_resolution;
    
        uint light_bits = 0;
    
        switch (light_info.type)
        {
            case LIGHT_TYPE_POINT:
            {                
                if (test_point_light(uv, uv_stride, light_info))
                {
                    light_bits = 1 << group_thread_id.x;
                }
            }
            break;            
            case LIGHT_TYPE_SPOT:
            {
                if (test_spot_light(uv, uv_stride, light_info))
                {
                    light_bits = 1 << group_thread_id.x;
                }
            }
            break;
        }
    
        // Put the result into the group shared array
        SharedData[group_index] = light_bits;
    }
    
    // Make sure everyone is caught up
    GroupMemoryBarrierWithGroupSync();
    
    // Start merging the results
    uint merge_step_size = 2;
    uint merge_prev_offset = 1;
    const uint adjusted_thread_index = group_thread_id.x + 1; // Need to adjust from 0-based indexing to make this work
    for (uint merge_level = 0; merge_level < 5; ++merge_level)
    {
        if ((adjusted_thread_index % merge_step_size) == 0)
        {
            // Current thread is the index we merge onto, read the value from the prev offset
            SharedData[group_index] |= SharedData[group_index - merge_prev_offset];
        }
        
        // Update the step sizes
        merge_step_size *= 2;
        merge_prev_offset *= 2;
        
        // Make sure everyone is ready before doing the next merge step
        GroupMemoryBarrierWithGroupSync();
    }
    
    // Final value ends up in the last thread
    if (adjusted_thread_index == LIGHTS_PER_GROUP)
    {
        // Write the value into the output buffer
        const uint tile_bitmask_offset = get_tile_bitmask_offset(group_id, group_thread_id);
        TileBitmasks[tile_bitmask_offset] = SharedData[group_index];
    }
}