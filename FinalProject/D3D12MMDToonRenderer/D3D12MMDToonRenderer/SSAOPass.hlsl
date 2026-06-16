#define SSAO_KERNEL_SIZE 16

Texture2D gNormalTexture   : register(t0);
Texture2D gPositionTexture : register(t1);

SamplerState gPointSampler : register(s0);

cbuffer SsaoConstantBuffer : register(b0)
{
    float4 gSamples[SSAO_KERNEL_SIZE];

    float4x4 gView;
    float4x4 gProjection;

    // x = radius
    // y = bias
    // z = intensity
    // w = reserved / enabled (currently always 1)
    float4 gParameters;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain(uint vertexID : SV_VertexID)
{
    PSInput output;

    float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };

    float2 uvs[3] =
    {
        float2(0.0f,  1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f,  1.0f)
    };

    output.position =
        float4(positions[vertexID], 0.0f, 1.0f);

    output.uv = uvs[vertexID];

    return output;
}

float Hash12(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float PSMain(PSInput input) : SV_TARGET
{
    if (gParameters.w < 0.5f)
    {
        return 1.0f;
    }
    
    const float radius = max(gParameters.x, 0.0001f);

    const float bias = max(gParameters.y, 0.0f);

    const float intensity = max(gParameters.z, 0.0001f);

    float4 positionSample =
        gPositionTexture.SampleLevel(
            gPointSampler,
            input.uv,
            0
        );

    // Position.a == 0 means no geometry was written to this pixel.
    if (positionSample.a < 0.5f)
        return 1.0f;

    float3 worldPosition =
        positionSample.xyz;

    float3 worldNormal =
        normalize(
            gNormalTexture.SampleLevel(
                gPointSampler,
                input.uv,
                0
            ).xyz * 2.0f - 1.0f
        );

    float3 viewPosition =
        mul(
            float4(worldPosition, 1.0f),
            gView
        ).xyz;

    float3 viewNormal =
        normalize(
            mul(
                float4(worldNormal, 0.0f),
                gView
            ).xyz
        );

    // A repeating 4x4 random-rotation pattern. This behaves like the
    // conventional SSAO noise tile without requiring another texture yet.
    float2 noiseCell =
        fmod(floor(input.position.xy), 4.0f);

    float randomAngle =
        Hash12(noiseCell) *
        6.28318530718f;

    float3 randomVector =
        float3(
            cos(randomAngle),
            sin(randomAngle),
            0.0f
        );

    float3 tangent =
        randomVector -
        viewNormal *
        dot(randomVector, viewNormal);

    if (dot(tangent, tangent) < 0.0001f)
    {
        float3 helper =
            abs(viewNormal.z) < 0.999f
                ? float3(0.0f, 0.0f, 1.0f)
                : float3(0.0f, 1.0f, 0.0f);

        tangent =
            cross(helper, viewNormal);
    }

    tangent = normalize(tangent);

    float3 bitangent =
        normalize(
            cross(viewNormal, tangent)
        );

    float occlusion = 0.0f;
    float validSampleCount = 0.0f;

    [unroll]
    for (int i = 0; i < SSAO_KERNEL_SIZE; ++i)
    {
        float3 sampleDirection =
            tangent    * gSamples[i].x +
            bitangent  * gSamples[i].y +
            viewNormal * gSamples[i].z;

        float3 samplePosition =
            viewPosition +
            sampleDirection * radius;

        float4 projectedPosition =
            mul(
                float4(samplePosition, 1.0f),
                gProjection
            );

        if (projectedPosition.w <= 0.0001f)
            continue;

        float2 sampleUV =
            projectedPosition.xy /
            projectedPosition.w;

        sampleUV =
            sampleUV *
            float2(0.5f, -0.5f) +
            0.5f;

        if (sampleUV.x < 0.0f ||
            sampleUV.x > 1.0f ||
            sampleUV.y < 0.0f ||
            sampleUV.y > 1.0f)
        {
            continue;
        }

        float4 sampledPosition =
            gPositionTexture.SampleLevel(
                gPointSampler,
                sampleUV,
                0
            );

        if (sampledPosition.a < 0.5f)
            continue;

        float sampledViewDepth =
            mul(
                float4(sampledPosition.xyz, 1.0f),
                gView
            ).z;

        float depthDifference =
            abs(
                viewPosition.z -
                sampledViewDepth
            );

        // Reject surfaces that are too far away from the current point.
        // This reduces dark halos across unrelated geometry.
        if (depthDifference > radius * 2.0f)
            continue;

        float rangeWeight =
            smoothstep(
                0.0f,
                1.0f,
                radius /
                max(depthDifference, 0.0001f)
            );

        // DirectX left-handed view space:
        // a smaller positive Z is closer to the camera.
        float blocked =
            sampledViewDepth <
            samplePosition.z - bias
                ? 1.0f
                : 0.0f;

        occlusion +=
            blocked * rangeWeight;

        validSampleCount += 1.0f;
    }

    if (validSampleCount < 0.5f)
        return 1.0f;

    float ao =
        1.0f -
        occlusion /
        validSampleCount;

    ao = pow(
        saturate(ao),
        intensity
    );

    return ao;
}
