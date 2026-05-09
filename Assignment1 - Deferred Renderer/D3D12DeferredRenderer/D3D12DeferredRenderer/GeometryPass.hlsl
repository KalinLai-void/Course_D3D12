cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 modelMatrix;
    float4x4 mvpMatrix;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct PSOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 worldPos : SV_TARGET2;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

PSInput VSMain(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD)
{
    PSInput result;
    result.position = mul(float4(position, 1.0f), mvpMatrix);
    result.worldPos = mul(float4(position, 1.0f), modelMatrix).xyz;
    result.normal = normalize(mul(float4(normal, 0.0f), modelMatrix).xyz);
    result.uv = uv;
    return result;
}

PSOutput PSMain(PSInput input)
{
    PSOutput output;
    float4 texColor = g_texture.Sample(g_sampler, input.uv);
    
    // Alpha Clipping
    if (texColor.a < 0.1f)
        discard;
        
    output.albedo = texColor;
    output.normal = float4(normalize(input.normal) * 0.5f + 0.5f, 1.0f); // [0, 1]
    output.worldPos = float4(input.worldPos, 1.0f);
    return output;
}