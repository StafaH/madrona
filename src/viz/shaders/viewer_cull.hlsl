#include "shader_common.h"
#include "../../render/vk/shaders/utils.hlsl"

[[vk::push_constant]]
CullPushConst push_const;

[[vk::binding(0, 0)]]
cbuffer ViewData {
    PackedViewData viewData;
};

[[vk::binding(1, 0)]]
StructuredBuffer<PackedInstanceData> engineInstanceBuffer;

[[vk::binding(2, 0)]]
cbuffer counts {
    uint32_t drawCount;
};

[[vk::binding(3, 0)]]
RWStructuredBuffer<DrawCmd> drawCommandBuffer;

// Asset descriptor bindings

[[vk::binding(0, 1)]]
StructuredBuffer<ObjectData> objectDataBuffer;

[[vk::binding(1, 1)]]
StructuredBuffer<MeshData> meshDataBuffer;

EngineInstanceData unpackEngineInstanceData(PackedInstanceData packed)
{
    const float4 d0 = packed.data[0];
    const float4 d1 = packed.data[1];
    const float4 d2 = packed.data[2];

    EngineInstanceData out;
    out.position = d0.xyz;
    out.rotation = float4(d1.xyz, d0.w);
    out.scale = float3(d1.w, d2.xy);
    out.objectID = asint(d2.z);

    return out;
}

// No actual culling performed yet
[numThreads(32, 1, 1)]
[shader("compute")]
void instanceCull(uint3 idx : SV_DispatchThreadID)
{
    uint32_t instance_id = idx.x;
    if (instance_id >= push_const.numInstances) {
        return;
    }

    EngineInstanceData instance_data = unpackEngineInstanceData(
        engineInstanceBuffer[instance_id]);

    ObjectData obj = objectDataBuffer[instance_data.objectID];

    uint draw_offset;
    InterlockedAdd(drawCount, obj.numMeshes, draw_offset);

    for (int32_t i = 0; i < obj.numMeshes; i++) {
        MeshData mesh = meshDataBuffer[obj.meshOffset + i];

        DrawCmd draw_cmd;
        draw_cmd.indexCount = mesh.numIndices;
        draw_cmd.instanceCount = 1;
        draw_cmd.firstIndex = mesh.indexOffset;
        draw_cmd.vertexOffset = mesh.vertexOffset;
        draw_cmd.firstInstance = instance_id;

        drawCommandBuffer[draw_offset + i] = draw_cmd;
    }
}