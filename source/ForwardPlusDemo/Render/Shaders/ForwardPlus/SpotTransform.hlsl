#include "Defines.hlsl"

StructuredBuffer<float4x4> SpotLightModels : register(t1);
RWStructuredBuffer<float4> SpotCullingData : register(u0);

float4x4 get_spot_light_model(uint spot_light_index)
{    
    return SpotLightModels[spot_light_index];
}

void write_spot_light_culling_data(uint spot_light_index, SpotLightCullingData culling_data)
{
    const uint base_offset = spot_light_index * SPOT_LIGHT_CULLING_DATA_STRIDE;
    
    for (uint current_row = 0; current_row < SPOT_LIGHT_CULLING_DATA_STRIDE - 1; ++current_row)
    {
        SpotCullingData[base_offset + current_row] = culling_data.projected_points[current_row];
    }
    
    SpotCullingData[base_offset + (SPOT_LIGHT_CULLING_DATA_STRIDE - 1)] = culling_data.z_parameters;
}

[numthreads(MAX_CS_THREAD_COUNT, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint spot_light_index = dispatch_thread_id.x;
    if (spot_light_index >= get_light_type_count(LIGHT_TYPE_SPOT))
    {
        return;
    }
    
    // Get the model matrix
    const float4x4 spot_light_model = transpose(get_spot_light_model(spot_light_index));
    
    // Compute the points of a pyramid that envelops the light cone
    float3 pyramid_points[5];
    
    pyramid_points[0] = spot_light_model[3].xyz;
    const float3 pyramid_base = pyramid_points[0] - spot_light_model[2].xyz;
    
    pyramid_points[1] = pyramid_base + spot_light_model[0].xyz + spot_light_model[1].xyz;
    pyramid_points[2] = pyramid_base - spot_light_model[0].xyz + spot_light_model[1].xyz;
    pyramid_points[3] = pyramid_base - spot_light_model[0].xyz - spot_light_model[1].xyz;
    pyramid_points[4] = pyramid_base + spot_light_model[0].xyz - spot_light_model[1].xyz;

    // Compute Z extents of the points  
    float pyramid_z[5];
    {
        for (int i = 0; i < 5; ++i)
        {
            pyramid_z[i] = dot(pyramid_points[i] - ForwardPlusCSConstants.camera_pos.xyz, ForwardPlusCSConstants.camera_front.xyz);
        }
    }

    float z_min = pyramid_z[0];
    float z_max = pyramid_z[0];
    
    {
        for (int i = 1; i < 5; ++i)
        {
            z_min = min(z_min, pyramid_z[i]);
            z_max = max(z_max, pyramid_z[i]);
        }
    }

    // Check whether we clip through the near or far plane (this info would be lost after projection)
    float cull;
    if ((z_min <= ForwardPlusParameters.z_near) && (z_max >= ForwardPlusParameters.z_far))
    {
        cull = 0.0;
    }
    else if (z_min <= ForwardPlusParameters.z_near)
    {
        cull = -1.0;
    }
    else
    {
        cull = 1.0;
    }
    
    SpotLightCullingData result;

    // Project the points onto the view plane 
    for (int i = 0; i < 5; ++i)
    {
        result.projected_points[i] = mul(float4(pyramid_points[i], 1.0f), ForwardPlusCSConstants.view_projection);
    }
    
    result.z_parameters = float4(cull, z_min, z_max, 0.0);
    
    // Write the result into the buffer
    write_spot_light_culling_data(spot_light_index, result);
}