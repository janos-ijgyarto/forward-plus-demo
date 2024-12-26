#include "Defines.hlsl"

struct VertexInput
{
    float4 pos : POSITION;
    float4 norm : NORMAL;
};

struct VertexOutput
{
    float4 clip_pos : SV_POSITION;    
    float4 world_pos : WORLDPOS;
    float4 view_pos : VIEWPOS;
    float4 norm : NORMAL;
};

cbuffer Camera : register(b1)
{
    struct
    {
        float4 world_position;
        float4x4 view;
        float4x4 view_projection;
    } Camera;
};

struct Material
{    
    float4 diffuse;
    float4 ambient;
};

cbuffer PerDrawData : register(b2)
{
    struct
    {
        float4x4 model;
        float4x4 inv_model;
        Material material;
    } PerDrawData;
};

StructuredBuffer<uint> ZBins : register(t0);
StructuredBuffer<uint> TileBitmasks : register(t1);
StructuredBuffer<LightData> LightDataBuffer : register(t2);

float3 process_light(uniform LightData light_data, VertexOutput pixel, float3 view_direction)
{
    const float3 pixel_to_light = light_data.position - pixel.world_pos.xyz;
    const float3 pixel_to_camera = Camera.world_position.xyz - pixel.world_pos.xyz;
    
    const float light_distance = length(pixel_to_light);
    
    // Phong diffuse
    const float3 pixel_to_light_norm = pixel_to_light / light_distance;
    const float diffuse_intensity = saturate(dot(pixel_to_light_norm, pixel.norm.xyz));
    
    const float3 diffuse = diffuse_intensity * light_data.diffuse * PerDrawData.material.diffuse.xyz;
    
    // Ambient
    const float3 ambient = light_data.ambient * PerDrawData.material.ambient.xyz;
    
    // Attenuation
    // TODO: allow for higher intensity, or more complex attenuation calculations?
    const float light_distance_norm = 1.0 - saturate(light_distance * light_data.inv_range);
    float attenuation = light_distance_norm * light_distance_norm;
    
    switch (light_data.info.type)
    {
        case LIGHT_TYPE_SPOT:
        {
            // Cone attenuation
            const float cos_light_angle = dot(-pixel_to_light_norm, normalize(light_data.direction));
            attenuation *= saturate((cos_light_angle - light_data.cos_outer_angle) * light_data.inv_cos_inner_angle);
        }
            break;
    }
    
	// Return intensity multiplied by attenuation
    return attenuation * (diffuse + ambient);
}

float3 compute_lighting(VertexOutput pixel, float3 view_direction)
{
    // Start from global light ambient
    float3 lighting = ForwardPlusParameters.global_light.ambient;

	// Make sure there are visible lights to process
    if (have_visible_lights() == false)
    {
        return lighting;
    }
     
    const LightCullingDataIndex culling_data_index = get_light_culling_data_index(pixel.clip_pos.xy, pixel.view_pos.z);
    const ZBin z_bin = read_z_bin(ZBins[culling_data_index.z_bin]);
		
	// Make sure Z bin is not empty
    if (is_z_bin_valid(z_bin) == false)
    {
        return lighting;
    }    
    
    const uint2 light_batch_min_max = uint2(z_bin.min / LIGHT_BATCH_SIZE, (z_bin.max / LIGHT_BATCH_SIZE) + 1);
    const uint batches_per_tile = integer_division_ceil(get_total_light_count(), LIGHT_BATCH_SIZE);
		
    const uint tile_flat_index = culling_data_index.tile_index.y * TILE_X_DIM + culling_data_index.tile_index.x;
    const uint light_batch_start_index = tile_flat_index * batches_per_tile;
		
	// Go over each light batch
    for (uint current_light_batch = light_batch_min_max.x; current_light_batch < light_batch_min_max.y; ++current_light_batch)
    {
        uint current_light_mask = TileBitmasks[light_batch_start_index + current_light_batch];
        const uint light_batch_offset = current_light_batch * LIGHT_BATCH_SIZE;

        const uint min_idx_in_batch = (z_bin.min - light_batch_offset);
        const uint max_idx_in_batch = (z_bin.max - light_batch_offset);
        if (min_idx_in_batch < LIGHT_BATCH_SIZE)
        {
            current_light_mask &= ~((1 << min_idx_in_batch) - 1); // remove lights below our min index
        }
        if (max_idx_in_batch < (LIGHT_BATCH_SIZE - 1))
        {
            current_light_mask &= ((1 << (max_idx_in_batch + 1)) - 1); // remove lights above our max index
        }

		// Check each set bit in the light mask
        while (current_light_mask)
        {
			// Get the index of the lowest bit that's currently set
            const uint local_light_index = firstbitlow(current_light_mask);
				
			// Remove the processed bit
            current_light_mask ^= (1 << local_light_index);
				
            const uint global_light_index = light_batch_offset + local_light_index;
            const LightData current_light_data = LightDataBuffer[global_light_index];

			// Make sure the light actually affects this Z bin
            const ZBin light_z_range = read_z_bin(current_light_data.info.z_range);
            if ((culling_data_index.z_bin < light_z_range.min) || (culling_data_index.z_bin > light_z_range.max))
            {
                continue;
            }
            
            lighting += process_light(current_light_data, pixel, view_direction);
        }
    }			

    return lighting;
}

VertexOutput vertex_shader(VertexInput input)
{
    VertexOutput output;
    
    output.world_pos = mul(input.pos, PerDrawData.model);
    
    output.clip_pos = mul(output.world_pos, Camera.view_projection);
    output.view_pos = mul(output.world_pos, Camera.view);
    output.norm = mul(normalize(input.norm), PerDrawData.inv_model);
    
    return output;
}

float4 pixel_shader(VertexOutput input) : SV_Target
{
    float3 view_direction = normalize(Camera.world_position.xyz - input.world_pos.xyz);
    return float4(compute_lighting(input, view_direction), 1.0f);
}