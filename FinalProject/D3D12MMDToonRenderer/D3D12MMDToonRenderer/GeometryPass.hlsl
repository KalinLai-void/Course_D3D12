cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 modelMatrix;
    float4x4 mvpMatrix;

    // xyz = PMX translation in model space, w = PMX scale.
    float4 pmxPositionScale;

    // xyz = PMX Euler rotation in radians.
    float4 pmxRotation;
};

cbuffer BoneConstantBuffer : register(b1)
{
    float4x4 g_BoneTransforms[1024];
};

cbuffer MaterialConstantBuffer : register(b2)
{
    // 0 = fully opaque
    // 1 = alpha cutout from diffuse texture alpha
    uint gOpacityMode;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float isCharacter : TEXCOORD1;
};

struct PSOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 worldPos : SV_TARGET2;
};

Texture2D g_diffuseTexture : register(t0);
Texture2D g_opacityTexture : register(t1);
SamplerState g_sampler : register(s0);

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
    float3 finalNormal = normal;

    if (isCharacterFlag > 0.5f)
    {
        float4 localPos = float4(position, 1.0f);
        float totalWeight =
            boneWeights.x + boneWeights.y +
            boneWeights.z + boneWeights.w;

        float4 skinnedPos = localPos;
        float3 skinnedNormal = normal;

        // Some PMX vertices can contain zero or non-normalized weights.
        // They still belong to the character, so keep them in the same PMX
        // transform instead of treating them as Sponza vertices.
        if (totalWeight > 0.0001f)
        {
            float4 weights = boneWeights / totalWeight;
            skinnedPos = float4(0.0f, 0.0f, 0.0f, 0.0f);
            skinnedNormal = float3(0.0f, 0.0f, 0.0f);

            [unroll]
            for (int i = 0; i < 4; ++i)
            {
                uint safeBoneIndex = min(boneIndices[i], 1023u);
                skinnedPos += weights[i] *
                    mul(localPos, g_BoneTransforms[safeBoneIndex]);
                skinnedNormal += weights[i] *
                    mul(float4(normal, 0.0f),
                        g_BoneTransforms[safeBoneIndex]).xyz;
            }
        }

        // Apply one uniform PMX object transform to every PMX vertex.
        // The previous version incorrectly kept a hard-coded scale of 12.0,
        // which made the camera sit inside the character.
        float3 p = skinnedPos.xyz * pmxPositionScale.w;

        const float c = cos(pmxRotation.y);
        const float s = sin(pmxRotation.y);

        finalPosition = float3(
            p.x * c + p.z * s,
            p.y,
            -p.x * s + p.z * c) + pmxPositionScale.xyz;

        float3 n = normalize(skinnedNormal);
        finalNormal = float3(
            n.x * c + n.z * s,
            n.y,
            -n.x * s + n.z * c);
    }

    result.position = mul(float4(finalPosition, 1.0f), mvpMatrix);
    result.worldPos = mul(float4(finalPosition, 1.0f), modelMatrix).xyz;
    result.normal = normalize(
        mul(float4(finalNormal, 0.0f), modelMatrix).xyz);
    result.uv = uv;
    result.isCharacter = isCharacterFlag;

    return result;
}

PSOutput PSMain(
    PSInput input,
    bool isFrontFace : SV_IsFrontFace)
{
    PSOutput output;

    float4 diffuseSample =
        g_diffuseTexture.Sample(g_sampler, input.uv);

    // The uploaded Sponza MTL contains no map_d entries. Only three
    // diffuse textures contain cutout alpha:
    //   sponza_thorn, vase_plant and chain_texture.
    // All other Sponza materials must remain opaque, even when their DDS
    // contains an unused alpha channel.
    if (gOpacityMode == 1)
    {
        clip(diffuseSample.a - 0.5f);
    }

    output.albedo = float4(diffuseSample.rgb, 1.0f);

    // CullMode is NONE because Sponza contains double-sided curtains and
    // foliage. Flip the interpolated normal on back faces so they do not
    // become incorrectly black.
    float3 shadingNormal = normalize(input.normal);
    if (!isFrontFace)
        shadingNormal = -shadingNormal;

    output.normal = float4(
        shadingNormal * 0.5f + 0.5f,
        input.isCharacter);

    // Alpha 1 marks a valid geometry pixel for the lighting pass.
    output.worldPos = float4(input.worldPos, 1.0f);

    return output;
}
