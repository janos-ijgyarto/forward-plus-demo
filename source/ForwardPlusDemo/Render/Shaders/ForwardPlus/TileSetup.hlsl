#include "Defines.hlsl"

StructuredBuffer<float4> SpotCullingData : register(t1);
StructuredBuffer<LightData> LightDataBuffer : register(t2);

RWStructuredBuffer<float4> TileCullingData : register(u0);

SpotLightCullingData get_spot_light_culling_data(uint spot_light_index)
{
    const uint base_offset = spot_light_index * SPOT_LIGHT_CULLING_DATA_STRIDE;
    SpotLightCullingData culling_data;
    
    // FIXME: D3D complains that it has to unroll this because of array indexing. Do we need to worry about it?    
    for (uint current_row = 0; current_row < SPOT_LIGHT_CULLING_DATA_STRIDE - 1; ++current_row)
    {
        culling_data.projected_points[current_row] = SpotCullingData[base_offset + current_row];
    }
    
    culling_data.z_parameters = SpotCullingData[base_offset + SPOT_LIGHT_CULLING_DATA_STRIDE - 1];
    return culling_data;
}

uint get_spot_light_data_offset(uint spot_light_index)
{
    const uint spot_light_data_start = get_light_type_count(LIGHT_TYPE_POINT) * POINT_LIGHT_STRIDE;
    return spot_light_data_start + (spot_light_index * SPOT_LIGHT_STRIDE);
}

float2 project_sphere_flat(float view_xy, float view_z, float inv_radius)
{
    // Use X/Y and Z to get the "hypotenuse", radius will be the "leg"
    const float2 view_xy_z = float2(view_xy, view_z);
    const float view_xy_z_length = length(view_xy_z);
    
    // Theta is the angle with which we can rotate the light center along this dimension to get its min and max projection
    const float sin_theta = 1.0f / (inv_radius * view_xy_z_length);

    float2 result;

    if (sin_theta < 0.999)
    {
        // Sphere still far enough in this dimension
        const float cos_theta = sqrt(1.0 - sin_theta * sin_theta);

        float2 rot_lo = mul(float2x2(cos_theta, -sin_theta, +sin_theta, cos_theta), view_xy_z);
        float2 rot_hi = mul(float2x2(cos_theta, +sin_theta, -sin_theta, cos_theta), view_xy_z);

        // Check rotated Z values. Negative means the points ended up behind us, implying it clips into the near plane
        // If non-negative, we also clamp to 1.0 and above, this prevents the ellipse from "ballooning" when light is near the edge of the view
        if (rot_lo.y <= 0.0)
        {
            rot_lo = float2(-1.0, 0.0);
        }
        else
        {
            rot_lo.y = max(rot_lo.y, 1.0);
        }
         
        if (rot_hi.y <= 0.0)
        {
            rot_hi = float2(+1.0, 0.0);
        }
        else
        {            
            rot_hi.y = max(rot_hi.y, 1.0);
        }

        result = float2(rot_lo.x / rot_lo.y, rot_hi.x / rot_hi.y);
    }
    else
    {
        // Sphere too close in this dimension
        result = float2(-1.0 / 0.0, +1.0 / 0.0);
    }

    return result;
}

float3x2 clip_single_output(float3 c0, float3 c1, float3 c2, float target)
{
    const float la = (target - c0.z) / (c2.z - c0.z);
    const float lb = (target - c1.z) / (c2.z - c1.z);
    
    const float3 c0_lerp = lerp(c0, c2, la);
    const float3 c1_lerp = lerp(c1, c2, lb);
    
    return float3x2(c0_lerp.xy, c1_lerp.xy, c2.xy);
}

struct ClippedDual3x2
{
    float3x2 clipped[2];
};

ClippedDual3x2 clip_dual_output(float3 c0, float3 c1, float3 c2, float target)
{
    const float l_ab = (target - c0.z) / (c1.z - c0.z);
    const float l_ac = (target - c0.z) / (c2.z - c0.z);

    const float3 ab = lerp(c0, c1, l_ab);
    const float3 ac = lerp(c0, c2, l_ac);

    ClippedDual3x2 result;
    
    result.clipped[0] = float3x2(ab.xy, c1.xy, ac.xy);
    result.clipped[1] = float3x2(ac.xy, c1.xy, c2.xy);
    
    return result;
}

float3x3 clip_single_output(float4 c0, float4 c1, float4 c2, float target)
{
    const float la = (target - c0.w) / (c2.w - c0.w);
    const float lb = (target - c1.w) / (c2.w - c1.w);
    
    const float4 c0_lerp = lerp(c0, c2, la);
    const float4 c1_lerp = lerp(c1, c2, lb);
    
    return float3x3(c0_lerp.xyz / target, c1_lerp.xyz / target, c2.xyz / c2.w);
}

struct ClippedDual3x3
{
    float3x3 clipped[2];
};

ClippedDual3x3 clip_dual_output(float4 c0, float4 c1, float4 c2, float target)
{
    const float l_ab = (target - c0.w) / (c1.w - c0.w);
    const float l_ac = (target - c0.w) / (c2.w - c0.w);

    const float4 ab = lerp(c0, c1, l_ab);
    const float4 ac = lerp(c0, c2, l_ac);

    ClippedDual3x3 result;
    
    result.clipped[0] = float3x3(ab.xyz / target, c1.xyz / c1.w, ac.xyz / target);
    result.clipped[1] = float3x3(ac.xyz / target, c1.xyz / c1.w, c2.xyz / c2.w);
    
    return result;
}

float cross_2d(float2 a, float2 b)
{
    return (a.x * b.y) - (a.y * b.x);
}

// FIXME: this was used to fix the compiler being confused by a variable being passed through functions and then returned
// Might be possible to fix via inout?
groupshared uint SpotTriangleCount[MAX_CS_THREAD_COUNT];

void setup_triangle(float3x2 clipped_points, float cull, uint spot_index, uint group_index)
{
    const float2 c0 = clipped_points[0];
    const float2 c1 = clipped_points[1];
    const float2 c2 = clipped_points[2];

    const float2 ab = c1 - c0;
    const float2 bc = c2 - c1;
    const float2 ca = c0 - c2;
    
    const float z = cross_2d(ab, -ca);
    
    if ((abs(z) < 0.000001f) || (sign(cull) == sign(z)))
    {
        return;
    }

    const float inv_z = 1.0 / z;

    const float3 base = inv_z * float3(cross_2d(ab, -c0), cross_2d(bc, -c1), cross_2d(ca, -c2));
    const float3 dx = inv_z * float3(-ab.y, -bc.y, -ca.y);
    const float3 dy = inv_z * float3(ab.x, bc.x, ca.x);

    const uint current_triangle_num = SpotTriangleCount[group_index];
    if (current_triangle_num < SPOT_LIGHT_MAX_TRIANGLES)
    {        
        const uint light_data_start = get_spot_light_data_offset(spot_index);
        const uint light_data_current_offset = light_data_start + (current_triangle_num * 4);
        
        TileCullingData[light_data_current_offset] = float4(base, 0.0f);
        TileCullingData[light_data_current_offset + 1] = float4(dx, z);
        TileCullingData[light_data_current_offset + 2] = float4(dy, inv_z);
        TileCullingData[light_data_current_offset + 3] = float4(min(min(c0, c1), c2), max(max(c0, c1), c2));
    }
    
    SpotTriangleCount[group_index] = current_triangle_num + 1;
}

static const uint4 TRIANGLE_SETUP_PARAMS[8] =
{
    uint4(0, 0, 0, 0),
    uint4(0, 1, 2, 0),
    uint4(1, 2, 0, 0),
    uint4(0, 1, 2, 0),
    uint4(2, 0, 1, 0),
    uint4(2, 0, 1, 0),
    uint4(1, 2, 0, 0),
    uint4(0, 0, 0, 0)
};

void setup_triangle(float3x3 triangle_points, float cull, uint spot_index, uint group_index)
{
    const float3 c0 = triangle_points[0];
    const float3 c1 = triangle_points[1];
    const float3 c2 = triangle_points[2];
    
    const bool3 clip_z = float3(c0.z, c1.z, c2.z) > 1.0f;
    const uint clip_code = uint(clip_z.x) + uint(clip_z.y) * 2u + uint(clip_z.z) * 4u;
    
    if (clip_code == 7)
    {
        // Early out
        // FIXME: does the compiler hate this more?
        return;
    }
    
    const uint4 triangle_setup_params = TRIANGLE_SETUP_PARAMS[clip_code];
    
    ClippedDual3x2 clipped_dual;
    const bool dual = (clip_code == 1) || (clip_code == 2) || (clip_code == 4);
    
    const float3 indexed_point_0 = triangle_points[triangle_setup_params.x];
    const float3 indexed_point_1 = triangle_points[triangle_setup_params.y];
    const float3 indexed_point_2 = triangle_points[triangle_setup_params.z];
      
    if (clip_code == 0)
    {
        clipped_dual.clipped[0] = float3x2(c0.xy, c1.xy, c2.xy);
    }
    else if (dual)
    {
        clipped_dual = clip_dual_output(indexed_point_0, indexed_point_1, indexed_point_2, 1.0);
    }
    else
    {
        clipped_dual.clipped[0] = clip_single_output(indexed_point_0, indexed_point_1, indexed_point_2, 1.0);
    }
    
    setup_triangle(clipped_dual.clipped[0], cull, spot_index, group_index);
    if (dual)
    {
        setup_triangle(clipped_dual.clipped[1], cull, spot_index, group_index);
    }
}

void setup_triangle(float4 c0, float4 c1, float4 c2, float cull, uint spot_index, uint group_index)
{
    // Check whether any of the points clip into the edges of the screen
    const float MIN_W = 1.0 / 1024.0;
    const bool3 clip_w = float3(c0.w, c1.w, c2.w) < MIN_W;
    const uint clip_code = uint(clip_w.x) + uint(clip_w.y) * 2u + uint(clip_w.z) * 4u;
    
    if (clip_code == 7)
    {
        // Early out
        // FIXME: does the compiler hate this more?
        return;
    }
    
    const uint4 triangle_setup_params = TRIANGLE_SETUP_PARAMS[clip_code];

    ClippedDual3x3 clipped_dual;
    const bool dual = (clip_code == 1) || (clip_code == 2) || (clip_code == 4);
    
    const float3x4 input_points = float3x4(c0, c1, c2);
    
    const float4 indexed_point_0 = input_points[triangle_setup_params.x];
    const float4 indexed_point_1 = input_points[triangle_setup_params.y];
    const float4 indexed_point_2 = input_points[triangle_setup_params.z];
    
    if (clip_code == 0)
    {
        clipped_dual.clipped[0] = float3x3(c0.xyz / c0.w, c1.xyz / c1.w, c2.xyz / c2.w);
    }
    else if (dual)
    {
        clipped_dual = clip_dual_output(indexed_point_0, indexed_point_1, indexed_point_2, MIN_W);
    }
    else
    {
        clipped_dual.clipped[0] = clip_single_output(indexed_point_0, indexed_point_1, indexed_point_2, MIN_W);
    }
    
    setup_triangle(clipped_dual.clipped[0], cull, spot_index, group_index);
    if (dual)
    {
        setup_triangle(clipped_dual.clipped[1], cull, spot_index, group_index);
    }
}

[numthreads(MAX_CS_THREAD_COUNT, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
    const uint light_index = dispatch_thread_id.x;
    if (light_index >= get_total_light_count())
    {
        return;
    }
    
    const LightInfo light_info = LightInfoBuffer[light_index];
    const bool is_point = (light_info.type == LIGHT_TYPE_POINT);

    switch (light_info.type)
    {
        case LIGHT_TYPE_POINT:
        {
            // Get the view position of the point light
            const LightData point_light_data = LightDataBuffer[light_index];
        
            float3 light_view_pos = mul(float4(point_light_data.position, 1.0f), ForwardPlusCSConstants.view).xyz;
            light_view_pos.y = -light_view_pos.y;
            
            // Calculate the ranges spanned by the light along the X and Y axes
            const float inv_radius = point_light_data.inv_range;
            float4 ranges = float4(project_sphere_flat(light_view_pos.x, light_view_pos.z, inv_radius), project_sphere_flat(light_view_pos.y, light_view_pos.z, inv_radius));
            const float xy_length = length(light_view_pos.xy);

            // Build a rotation matrix
            float2x2 clip_transform;
            if (xy_length < 0.00001f)
            {
                clip_transform = float2x2(1.0f, 0.0f, 0.0f, 1.0f);
            }
            else
            {
                const float inv_xy_length = 1.0f / xy_length;
                clip_transform = float2x2(light_view_pos.x, -light_view_pos.y, light_view_pos.y, light_view_pos.x) * inv_xy_length;
            }

            // Rotate the light onto the X axis
            const float2 transformed_xy = mul(light_view_pos.xy, clip_transform);
            
            // Compute the ranges for the rotated points (this should give us an ellipse)
            const float4 transformed_ranges = float4(project_sphere_flat(transformed_xy.x, light_view_pos.z, inv_radius), project_sphere_flat(transformed_xy.y, light_view_pos.z, inv_radius));
        
            // Check if we have a valid ellipse
            // If not, we'll just be using the ranges
            const bool ellipse = all(!isinf(transformed_ranges));

            // Get the ellipse center and radius
            const float2 center = (transformed_ranges.xz + transformed_ranges.yw) * 0.5f;
            const float2 ellipse_radius = transformed_ranges.yw - center;

            // Project ranges to screen space ("clip_scale" is derived from the projection matrix)
            ranges = ranges * ForwardPlusCSConstants.clip_scale.xxyy;
        
            // Write the results to the relevant offset
            const uint point_light_data_offset = light_info.index * POINT_LIGHT_STRIDE;
        
            TileCullingData[point_light_data_offset] = ranges.xwyz;
            TileCullingData[point_light_data_offset + 1] = transformed_ranges;
            TileCullingData[point_light_data_offset + 2] = float4(clip_transform[0], clip_transform[1]);
            TileCullingData[point_light_data_offset + 3] = float4(float(ellipse), 1.0 / ellipse_radius.xy, 0.0);
        }
        break;
        case LIGHT_TYPE_SPOT:
        {
            const SpotLightCullingData spot_culling_data = get_spot_light_culling_data(light_info.index);
            float triangle_count_float = asfloat(0xffffffffu);
            
            if (spot_culling_data.z_parameters.x != 0.0)
            {
                // Reset triangle counter
                // FIXME: use inout and remove the need for groupshared?
                SpotTriangleCount[group_index] = 0;
                
                // Get the projected points of the spot light pyramid
                const float4 c0 = spot_culling_data.projected_points[0];
                const float4 c1 = spot_culling_data.projected_points[1];
                const float4 c2 = spot_culling_data.projected_points[2];
                const float4 c3 = spot_culling_data.projected_points[3];
                const float4 c4 = spot_culling_data.projected_points[4];

                // For each combination of points, we check whether any of the points are clipped (i.e outside the view)
                // If so, we need to modify the triangles that we save
                // FIXME: might be easier to just skip this process and have a bit of redundancy by rasterizing all triangles?
                setup_triangle(c0, c1, c2, spot_culling_data.z_parameters.x, light_info.index, group_index);
                setup_triangle(c0, c2, c3, spot_culling_data.z_parameters.x, light_info.index, group_index);
                setup_triangle(c0, c3, c4, spot_culling_data.z_parameters.x, light_info.index, group_index);
                setup_triangle(c0, c4, c1, spot_culling_data.z_parameters.x, light_info.index, group_index);
                setup_triangle(c2, c1, c3, spot_culling_data.z_parameters.x, light_info.index, group_index);
                setup_triangle(c4, c3, c1, spot_culling_data.z_parameters.x, light_info.index, group_index);
            
                triangle_count_float = asfloat(SpotTriangleCount[group_index]);
            }
            
            // Save the triangle count
            const uint spot_data_offset = get_spot_light_data_offset(light_info.index);
            TileCullingData[spot_data_offset].w = triangle_count_float;
        }
        break;
    }
}