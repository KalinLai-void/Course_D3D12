cbuffer ShadowConstantBuffer : register(b0)
{
    float4x4 modelMatrix;
    float4x4 lightViewProj;

    // Must match the PMX object transform used by GeometryPass.hlsl.
    float4 pmxPositionScale;
    float4 pmxRotation;
};

cbuffer BoneConstantBuffer : register(b1)
{
    float4x4 g_BoneTransforms[1024];
};

Texture2D gDiffuseTexture : register(t0);
SamplerState gShadowMaterialSampler : register(s0);

cbuffer ShadowMaterialConstants : register(b2)
{
    // 0 = opaque
    // 1 = alpha cutout from diffuse alpha
    uint gOpacityMode;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD0,
    uint4 boneIndices : BLENDINDICES,
    float4 boneWeights : BLENDWEIGHT,
    float isCharacterFlag : TEXCOORD1)
{
    PSInput result;

    float3 finalPosition = position;

    if (isCharacterFlag > 0.5f)
    {
        float4 localPos =
            float4(position, 1.0f);

        float totalWeight =
            boneWeights.x +
            boneWeights.y +
            boneWeights.z +
            boneWeights.w;

        float4 skinnedPos = localPos;

        if (totalWeight > 0.0001f)
        {
            float4 weights =
                boneWeights / totalWeight;

            skinnedPos =
                float4(0.0f, 0.0f, 0.0f, 0.0f);

            [unroll]
            for (int i = 0; i < 4; ++i)
            {
                uint safeBoneIndex =
                    min(boneIndices[i], 1023u);

                skinnedPos +=
                    weights[i] *
                    mul(
                        localPos,
                        g_BoneTransforms[safeBoneIndex]);
            }
        }

        float3 p =
            skinnedPos.xyz *
            pmxPositionScale.w;

        const float c =
            cos(pmxRotation.y);

        const float s =
            sin(pmxRotation.y);

        finalPosition =
            float3(
                p.x * c + p.z * s,
                p.y,
                -p.x * s + p.z * c) +
            pmxPositionScale.xyz;
    }

    float4 worldPos =
        mul(
            float4(finalPosition, 1.0f),
            modelMatrix);

    result.position =
        mul(
            worldPos,
            lightViewProj);

    result.uv = uv;

    return result;
}

// Depth-only pass with alpha testing.
// Without this clip, transparent PMX hair/accessory pixels still write depth
// and create large false shadows on the face and body.
void PSMain(PSInput input)
{
    if (gOpacityMode == 1u)
    {
        const float alpha =
            gDiffuseTexture.Sample(
                gShadowMaterialSampler,
                input.uv).a;

        clip(alpha - 0.5f);
    }
}
