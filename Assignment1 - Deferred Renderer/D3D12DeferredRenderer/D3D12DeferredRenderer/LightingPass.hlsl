cbuffer DebugBuffer : register(b1)
{
    uint renderMode; // 0:Depth, 1:Normal, 2:Albedo, 3:Color
    float3 cameraPos;
};

Texture2D g_gbufferAlbedo : register(t0);
Texture2D g_gbufferNormal : register(t1);
Texture2D g_gbufferPos : register(t2);
SamplerState g_sampler : register(s0);

struct PSInputLight
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

PSInputLight VSLighting(uint vertexID : SV_VertexID)
{
    PSInputLight result;
    // generating trangle for full-screen
    result.uv = float2((vertexID << 1) & 2, vertexID & 2);
    result.position = float4(result.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return result;
}

float4 PSLighting(PSInputLight input) : SV_TARGET
{
    float3 albedo = g_gbufferAlbedo.Sample(g_sampler, input.uv).rgb;
    float3 rawNormal = g_gbufferNormal.Sample(g_sampler, input.uv).xyz;
    float3 normal = normalize(rawNormal * 2.0f - 1.0f); // [-1, 1]
    float3 worldPos = g_gbufferPos.Sample(g_sampler, input.uv).xyz;

    // Direction to Light
    float3 L = normalize(float3(-0.577f, -0.577f, -0.577f));
    float3 V = normalize(cameraPos - worldPos);
    float3 H = normalize(L + V);

    // Blinn-Phong
    float ambient = 0.2f;
    float diffuse = max(dot(normal, L), 0.0f);
    float specular = pow(max(dot(normal, H), 0.0f), 32.0f);

    float3 finalColor = albedo * (diffuse + ambient) + float3(1, 1, 1) * specular;
    
    switch (renderMode)
    {
        case 0: // Depth
            float dist = length(cameraPos - worldPos) / 100.0f;
            return float4(dist, dist, dist, 1.0f);
        case 1: // Normal
            return float4(normal * 0.5f + 0.5f, 1.0f);
        case 2: // Albedo
            return float4(albedo, 1.0f);
        default: // Final Color
            return float4(finalColor, 1.0f);
    }
}