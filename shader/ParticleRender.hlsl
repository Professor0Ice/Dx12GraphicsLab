struct Particle
{
    float3 position;
    float padding0;
    float3 velocity;
    float padding1;
};

cbuffer ParticleDrawConstants : register(b0)
{
    row_major float4x4 gViewProjection;
    float4 gCameraRight;
    float4 gCameraUp;
    float4 gColorAndSize;
};

StructuredBuffer<Particle> gParticles : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    Particle particle = gParticles[vertexId];
    VSOutput output;
    output.position = float4(particle.position, 1.0f);
    output.worldPosition = particle.position;
    return output;
}

struct GSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[maxvertexcount(4)]
void GSMain(point VSOutput input[1], inout TriangleStream<GSOutput> stream)
{
    const float3 right = normalize(gCameraRight.xyz);
    const float3 up = normalize(gCameraUp.xyz);
    const float3 normal = -normalize(cross(right, up));
    const float halfSize = gColorAndSize.w;
    const float3 center = input[0].worldPosition;

    const float3 corners[4] =
    {
        center + (-right + up) * halfSize,
        center + (-right - up) * halfSize,
        center + ( right + up) * halfSize,
        center + ( right - up) * halfSize
    };
    const float2 uvs[4] =
    {
        float2(0.0f, 0.0f), float2(0.0f, 1.0f),
        float2(1.0f, 0.0f), float2(1.0f, 1.0f)
    };

    [unroll]
    for (uint index = 0; index < 4; ++index)
    {
        GSOutput output;
        output.position = mul(float4(corners[index], 1.0f), gViewProjection);
        output.worldPosition = corners[index];
        output.normal = normal;
        output.uv = uvs[index];
        output.color = float4(gColorAndSize.rgb, 1.0f);
        stream.Append(output);
    }
}

struct GBufferOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 position : SV_TARGET2;
};

GBufferOutput PSMain(GSOutput input)
{
    GBufferOutput output;
    // Мягкая круглая форма без alpha blending: отброшенные пиксели не меняют G-buffer.
    const float2 centeredUv = input.uv * 2.0f - 1.0f;
    clip(1.0f - dot(centeredUv, centeredUv));
    output.albedo = input.color;
    output.normal = float4(normalize(input.normal), 0.28f);
    output.position = float4(input.worldPosition, 0.0f);
    return output;
}
