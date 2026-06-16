cbuffer DebugBuffer : register(b1)
{
    // 0: Depth
    // 1: Normal
    // 2: Albedo
    // 3: Final Color
    // 4: SSAO
    uint renderMode;

    float3 cameraPos;
    float exposure;
};

Texture2D g_gbufferAlbedo : register(t0);
Texture2D g_gbufferNormal : register(t1);
Texture2D g_gbufferPos : register(t2);
Texture2D g_ssaoTexture : register(t3);
SamplerState g_sampler : register(s0);

static const float2 offsets[4] = { float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1) };

struct PSInputLight
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

PSInputLight VSLighting(uint vertexID : SV_VertexID)
{
    PSInputLight result;
    // generating triangle for full-screen
    result.uv = float2((vertexID << 1) & 2, vertexID & 2);
    result.position = float4(result.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return result;
}

float4 PSLighting(PSInputLight input) : SV_TARGET
{
    // SSAO Debug Mode
    if (renderMode == 4)
    {
        float ao =
            g_ssaoTexture.Sample(
                g_sampler,
                input.uv
            ).r;

        return float4(
            ao,
            ao,
            ao,
            1.0f
        );
    }

    float4 positionSample =
        g_gbufferPos.Sample(
            g_sampler,
            input.uv
        );
    
    
    // Pixels removed by alpha clipping keep Position.a = 0.  Do not run
    // lighting or character outline code on those empty G-buffer pixels.
    if (positionSample.a < 0.5f)
    {
        return float4(0.0f, 0.2f, 0.4f, 1.0f);
    }

    float3 worldPos = positionSample.xyz;
    float3 albedo = g_gbufferAlbedo.Sample(g_sampler, input.uv).rgb;
    
    float4 normalSample = g_gbufferNormal.Sample(g_sampler, input.uv);
    float3 rawNormal = normalSample.xyz;
    float isCharacter = normalSample.a; // 讀取 Character 標記
    
    float3 normal = normalize(rawNormal * 2.0f - 1.0f);

    float3 L = normalize(float3(-0.577f, -0.577f, -0.577f));
    float3 V = normalize(cameraPos - worldPos);
    float3 H = normalize(L + V);

    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    float outline = 1.0f;

    // ==========================================
    // 1. 螢幕空間輪廓線 (僅限角色)
    // ==========================================
    if (isCharacter > 0.5f)
    {
        uint texWidth, texHeight;
        g_gbufferNormal.GetDimensions(texWidth, texHeight);
        float2 texelSize = float2(1.0f / (float) texWidth, 1.0f / (float) texHeight);

        // 陣列已移至上方全域範圍
        for (int i = 0; i < 4; i++)
        {
            float2 neighborUV = input.uv + offsets[i] * texelSize;
            float4 nNeighborSample = g_gbufferNormal.SampleLevel(g_sampler, neighborUV, 0);
            float3 nNeighbor = normalize(nNeighborSample.xyz * 2.0f - 1.0f);
            float3 pNeighbor = g_gbufferPos.SampleLevel(g_sampler, neighborUV, 0).xyz;

            float deltaN = 1.0f - dot(normal, nNeighbor);
            float deltaP = length(worldPos - pNeighbor);

            if (deltaN > 0.25f || deltaP > 0.4f)
            {
                outline = 0.0f;
                break;
            }
        }
    }

    // ==========================================
    // 2. 雙軌渲染邏輯 (NPR vs PBR)
    // ==========================================
    if (isCharacter > 0.5f)
    {
        // 【動漫風格 (Cel Shading)】
        float NdotL = dot(normal, L);
        float halfLambert = NdotL * 0.5f + 0.5f;
        
        float toonDiffuse = 1.0f;
        if (halfLambert < 0.45f)
            toonDiffuse = 0.6f;

        float rim = 1.0f - max(dot(normal, V), 0.0f);
        float rimIntensity = pow(abs(rim), 4.0f) * max(dot(normal, L), 0.0f);
        float toonRim = rimIntensity > 0.3f ? 0.2f : 0.0f;

        finalColor = albedo * toonDiffuse + float3(1.0f, 1.0f, 1.0f) * toonRim;
        finalColor *= outline;
    }
    else
    {
        // 【寫實風格 (Blinn-Phong) - 用於 Sponza】
        float ambient = 0.28f;
        float NdotL = max(dot(normal, L), 0.0f);
        float specular = 0.0f;
        if (NdotL > 0.0f)
        {
            specular = pow(max(dot(normal, H), 0.0f), 32.0f) * 0.08f;
        }

        finalColor = albedo * (NdotL + ambient) + specular.xxx;
        
        finalColor *= exposure;
        
        // 1. ACES Tone Mapping
        finalColor = (finalColor * (2.51f * finalColor + 0.03f)) / (finalColor * (2.43f * finalColor + 0.59f) + 0.14f);

        // 2. Gamma Correction (將 Linear 轉回 sRGB 輸出給螢幕)
        finalColor = pow(max(finalColor, 0.0f), 1.0f / 2.2f);
    }

    // ==========================================
    // 3. 輸出結果
    // ==========================================
    finalColor = saturate(finalColor);
    
    if (renderMode == 0) // Depth
    {
        float dist = length(cameraPos - worldPos) / 100.0f;
        return float4(dist, dist, dist, 1.0f);
    }
    else if (renderMode == 1) // Normal
    {
        return float4(normal * 0.5f + 0.5f, 1.0f);
    }
    else if (renderMode == 2) // Albedo
    {
        return float4(albedo, 1.0f);
    }
    else // Final Color
    {
        return float4(finalColor, 1.0f);
    }
}