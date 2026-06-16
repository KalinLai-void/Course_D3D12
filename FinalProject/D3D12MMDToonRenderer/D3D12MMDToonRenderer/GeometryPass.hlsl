cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 modelMatrix;
    float4x4 mvpMatrix;
};

cbuffer BoneConstantBuffer : register(b1)
{
    float4x4 g_BoneTransforms[256];
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float isCharacter : TEXCOORD1;
};

struct PSOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 worldPos : SV_TARGET2;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

PSInput VSMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    uint4 boneIndices : BLENDINDICES,
    float4 boneWeights : BLENDWEIGHT
)
{
    PSInput result;
    
    //float4 localPos = float4(position, 1.0f);
    //float4 skinnedPos = float4(0.0f, 0.0f, 0.0f, 0.0f);
    //float3 skinnedNormal = float3(0.0f, 0.0f, 0.0f);
    
    //// check if has bones weight
    //float totalWeight = boneWeights.x + boneWeights.y + boneWeights.z + boneWeights.w;
    
    //if (totalWeight > 0.01f)
    //{
    //    // 4-bones Linear Blend Skinning
    //    for (int i = 0; i < 4; ++i)
    //    {
    //        skinnedPos += boneWeights[i] * mul(localPos, g_BoneTransforms[boneIndices[i]]);
    //        skinnedNormal += boneWeights[i] * mul(float4(normal, 0.0f), g_BoneTransforms[boneIndices[i]]).xyz;
    //    }
    //    position = skinnedPos.xyz;
    //    normal = normalize(skinnedNormal);
    //    result.isCharacter = 1.0f;
    //}
    //else
    //{
    //    result.isCharacter = 0.0f; // static scene objs
    //}
    
    //result.position = mul(float4(position, 1.0f), mvpMatrix);
    //result.worldPos = mul(float4(position, 1.0f), modelMatrix).xyz;
    //result.normal = normalize(mul(float4(normal, 0.0f), modelMatrix).xyz);
    //result.uv = uv;
    //return result;
    
    if (boneWeights.w > 0.9f)
    {
        result.isCharacter = 1.0f;
    }
    else
    {
        result.isCharacter = 0.0f;
    }
    
    // 直接套用標準變換矩陣，省去未初始化的骨骼矩陣運算
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
    if (texColor.a < 0.5f)
        discard;
        
    output.albedo = texColor; 
    
    // isCharacter tag in Normal Buffer's Alpha channel
    output.normal = float4(normalize(input.normal) * 0.5f + 0.5f, input.isCharacter);
    
    output.worldPos = float4(input.worldPos, 1.0f);
    return output;
}