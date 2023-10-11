#include "Defines.hlsl"

#ifndef Z_BINNING_GROUP_SIZE
#define Z_BINNING_GROUP_SIZE 128
#endif

#define Z_RANGE_BITSET_COUNT 4

cbuffer ZBinningConstants : register(b2)
{
    struct
    {
        uint invocation; // This indicates which set of lights we are processing
        uint3 _padding;
    } ZBinningConstants;
};

struct ZRangeData
{
    uint4 z_range_bitmask;
};

uint4 get_light_z_range_bits(uint base_index, LightInfo light_info)
{       
    // Convert the indices to a bitmask spread across the current domain (128 bins in total)
    const ZBin light_z_range = read_z_bin(light_info.z_range);
    
    uint4 indices = base_index + uint4(0, 32, 64, 96);
    int4 shift_amount;

    shift_amount = max(int4(light_z_range.min - indices), int4(0, 0, 0, 0));
    uint4 lo_mask = uint4(~0u, ~0u, ~0u, ~0u) << shift_amount;
    lo_mask = lo_mask * (1 - (shift_amount >= int4(32, 32, 32, 32)));

    shift_amount = max(int4(indices - (light_z_range.max - 31)), int4(0, 0, 0, 0));
    uint4 hi_mask = uint4(~0u, ~0u, ~0u, ~0u) >> shift_amount;
    hi_mask = hi_mask * (1 - (shift_amount >= int4(32, 32, 32, 32)));
    
    // The final result is the overlap between the low and high masks
    return lo_mask & hi_mask;
}

uint write_z_bin(ZBin z_bin)
{
    uint z_bin_data = (z_bin.min & Z_BIN_MIN_MASK);
    z_bin_data |= (z_bin.max << 16);
    
    return z_bin_data;
}

ZBin get_z_bin_range(uint4 collision_bits, uint light_base_offset)
{
    ZBin z_bin;
    z_bin.min = 0xFFFF;
    z_bin.max = 0;
    
    for (uint bitset_index = 0; bitset_index < Z_RANGE_BITSET_COUNT; ++bitset_index)
    {
        const uint current_bits = collision_bits[bitset_index];
        if (current_bits)
        {
            const uint base_offset = light_base_offset + (bitset_index * 32);
            z_bin.min = min(z_bin.min, firstbitlow(current_bits) + base_offset);
            z_bin.max = max(z_bin.max, firstbithigh(current_bits) + base_offset);
        }
    }

    return z_bin;
}

groupshared ZRangeData SharedData[Z_BINNING_GROUP_SIZE];

uint read_collision_bit_rows(uint column_bit_chunk_index, uint column_bit_mask, uint current_bit_chunk_index)
{
    uint result = 0;
    const uint start_row = current_bit_chunk_index * 32;
    for (uint current_row = 0; current_row < 32; ++current_row)
    {
        // Get the bits at the relevant offset
        const uint current_row_bits = SharedData[start_row + current_row].z_range_bitmask[column_bit_chunk_index];
        
        // If the bit is set, then set the bit corresponding to this row in the result
        result |= (current_row_bits & column_bit_mask) ? (1 << current_row) : 0;
    }
    
    return result;
}

RWStructuredBuffer<uint> ZBins : register(u0);

[numthreads(Z_BINNING_GROUP_SIZE, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{    
    // First we have each thread compute the Z range for each light
    // FIXME: based on the source, we could have each thread iterate over multiple lights (step by NUM_LIGHTS / GROUP_SIZE each time)
    // but that might confuse the compiler. Needs profiling to see if it's better than multiple dispatches
    const uint light_base_offset = (ZBinningConstants.invocation * Z_BINNING_GROUP_SIZE);
    const uint light_index = light_base_offset + group_index;
    
    if(light_index < get_total_light_count())
    {
        const LightInfo light_info = LightInfoBuffer[light_index];
    
        // Store the resulting bitmask in group shared memory
        SharedData[group_index].z_range_bitmask = get_light_z_range_bits(group_id.x * Z_BINNING_GROUP_SIZE, light_info);
    }
    else
    {
        // Past the max lights
        SharedData[group_index].z_range_bitmask = uint4(0, 0, 0, 0);
    }
    
    // Memory barrier so everyone is finished by when we get here
    GroupMemoryBarrierWithGroupSync();
    
    // Make sure this thread is actually working on a valid Z bin
    const uint global_index = dispatch_thread_id.x;
    if (global_index < Z_BIN_COUNT)
    {    
        // Calculate the index and mask for the bits we need to extract from the shared data    
        const uint column_bit_chunk_index = group_index / 32;
        const uint column_local_index = group_index - (column_bit_chunk_index * 32);
        const uint column_bit_mask = 1 << column_local_index;
    
        uint4 collision_bits = uint4(0, 0, 0, 0);
    
        // Iterate over the "collision matrix" rows, extract the bit corresponding to the "column" that is this Z bin
        // NOTE: splitting because D3D prefers this over indexing into uint4, might be better to just let it unroll?
        collision_bits.x = read_collision_bit_rows(column_bit_chunk_index, column_bit_mask, 0);
        collision_bits.y = read_collision_bit_rows(column_bit_chunk_index, column_bit_mask, 1);
        collision_bits.z = read_collision_bit_rows(column_bit_chunk_index, column_bit_mask, 2);
        collision_bits.w = read_collision_bit_rows(column_bit_chunk_index, column_bit_mask, 3);
    
        // Convert to Z bin
        ZBin z_bin_data = get_z_bin_range(collision_bits, light_base_offset);
    
        // Use the result to update the min and max in the output
        ZBin prev_z_bin_data = read_z_bin(ZBins[global_index]);
    
        z_bin_data.min = min(prev_z_bin_data.min, z_bin_data.min);
        z_bin_data.max = max(prev_z_bin_data.max, z_bin_data.max);

        // Write the final result into the buffer
        const uint result = write_z_bin(z_bin_data);
        ZBins[global_index] = result;
    }
}