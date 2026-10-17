struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[6] = { float2(-1.0f,  1.0f), float2( 1.0f,  1.0f), float2( 1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f, -1.0f), float2(-1.0f, -1.0f) };

    VSOutput output;
    const float2 position = positions[vertexId];
    output.position = float4(position, 0.0f, 1.0f);
    output.uv = float2(position.x * 0.5f + 0.5f, 0.5f - position.y * 0.5f);
    return output;
}
