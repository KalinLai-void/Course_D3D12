cbuffer DebugBuffer : register(b1)
{
    // 0: Depth
    // 1: Normal
    // 2: Albedo
    // 3: Final Color
    // 4: SSAO
    // 5: Shadow factor
    uint renderMode;
    float3 cameraPos;

    float exposure;
    float pointLightRadius;
    float directionalLightDirX;
    float padding0;

    float3 pointLightPos;
    float directionalLightDirY;

    float3 pointLightColor;
    float directionalLightDirZ;

    float4x4 lightViewProj;
};

cbuffer SceneLightToggle : register(b2)
{
    uint gSceneLightsEnabled;
};

cbuffer ShadowFilterToggle : register(b3)
{
    // 0 = one hard comparison
    // 1 = explicit 3x3 percentage-closer filtering
    uint gEnablePcf;
};

Texture2D g_gbufferAlbedo : register(t0);
Texture2D g_gbufferNormal : register(t1);
Texture2D g_gbufferPos    : register(t2);
Texture2D g_ssaoTexture   : register(t3);
Texture2D g_shadowMap     : register(t4);

SamplerState g_sampler : register(s0);
SamplerComparisonState g_shadowSampler : register(s1);

static const float2 outlineOffsets[4] =
{
    float2(-1.0f,  0.0f),
    float2( 1.0f,  0.0f),
    float2( 0.0f, -1.0f),
    float2( 0.0f,  1.0f)
};

// The PMX character keeps its stylized toon self-shading.
// Only a small amount of the scene shadow map is blended onto it.
// 0.0 = character does not receive the scene shadow map, but still casts.
// 1.0 = character receives the full scene shadow map.
static const float CHARACTER_SHADOW_MAP_STRENGTH = 0.15f;

struct PSInputLight
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

PSInputLight VSLighting(uint vertexID : SV_VertexID)
{
    PSInputLight result;

    result.uv = float2((vertexID << 1) & 2, vertexID & 2);
    result.position = float4(result.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

    return result;
}

float ComputeShadowFactor(
    float3 worldPos,
    float3 normal,
    float3 surfaceToDirectionalLight,
    float isCharacter)
{
    const float3 N = normalize(normal);
    const float3 L = normalize(surfaceToDirectionalLight);

    const float nDotL =
        saturate(dot(N, L));

    const float grazingAmount =
        1.0f - nDotL;

    // Receiver normal-offset bias:
    // move the lookup point slightly toward the visible surface normal.
    // This removes self-shadow acne without needing an excessively large
    // depth bias that would detach the shadow from the character.
    const float normalOffset =
        isCharacter > 0.5f
            ? lerp(0.015f, 0.055f, grazingAmount)
            : lerp(0.005f, 0.030f, grazingAmount);

    const float3 biasedWorldPos = worldPos + N * normalOffset;

    float4 lightSpacePos = mul(float4(biasedWorldPos, 1.0f), lightViewProj);

    if (lightSpacePos.w <= 0.0001f)
        return 1.0f;

    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    float2 shadowUV = float2(projCoords.x * 0.5f + 0.5f, -projCoords.y * 0.5f + 0.5f);

    // DirectX NDC depth is [0, 1].
    if (shadowUV.x < 0.0f ||
        shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f ||
        shadowUV.y > 1.0f ||
        projCoords.z < 0.0f ||
        projCoords.z > 1.0f)
    {
        return 1.0f;
    }

    // Keep the comparison bias small because the world-space normal offset
    // above already handles most acne. The old 0.00035-0.0025 range was large
    // enough to produce visible peter panning on the PMX model.
    const float comparisonBias =
        isCharacter > 0.5f
            ? lerp(0.00004f, 0.00014f, grazingAmount)
            : lerp(0.00006f, 0.00025f, grazingAmount);

    // F OFF: a single comparison produces a hard, aliased shadow edge.
    // This is useful as the before/after comparison for the PCF requirement.
    if (gEnablePcf == 0u)
    {
        return g_shadowMap.SampleCmpLevelZero(
            g_shadowSampler,
            shadowUV,
            projCoords.z - comparisonBias);
    }

    // F ON: explicit 3x3 percentage-closer filtering.
    // The compact 3x3 kernel softens jagged edges while limiting light leaks.
    uint shadowWidth;
    uint shadowHeight;

    g_shadowMap.GetDimensions(shadowWidth, shadowHeight);

    float2 shadowTexelSize = float2(
                1.0f / float(shadowWidth), 
                1.0f / float(shadowHeight));

    float visibility = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            visibility += g_shadowMap.SampleCmpLevelZero(
                    g_shadowSampler,
                    shadowUV + float2(float(x),
                                      float(y)) * shadowTexelSize,
                                      projCoords.z - comparisonBias);
        }
    }

    return visibility / 9.0f;
}

float4 PSLighting(PSInputLight input) : SV_TARGET
{
    // Temporary 5x5 SSAO smoothing.
    float ao = 0.0f;
    uint aoWidth;
    uint aoHeight;
    g_ssaoTexture.GetDimensions(
        aoWidth,
        aoHeight);

    float2 aoTexelSize = float2(
            1.0f / float(aoWidth),
            1.0f / float(aoHeight));

    [unroll]
    for (int x = -2; x <= 2; ++x)
    {
        [unroll]
        for (int y = -2; y <= 2; ++y)
        {
            ao += g_ssaoTexture.SampleLevel(
                    g_sampler,
                    input.uv +
                    float2(x, y) *
                    aoTexelSize,
                    0).r;
        }
    }

    ao /= 25.0f;

    float4 positionSample = g_gbufferPos.Sample(g_sampler, input.uv);

    if (positionSample.a < 0.5f)
    {
        return float4(0.0f, 0.2f, 0.4f, 1.0f);
    }

    float3 worldPos = positionSample.xyz;

    float3 albedo = g_gbufferAlbedo.Sample(g_sampler, input.uv).rgb;

    float4 normalSample = g_gbufferNormal.Sample(g_sampler, input.uv);
    float3 normal = normalize(normalSample.xyz * 2.0f - 1.0f);

    float isCharacter = normalSample.a;

    float3 V = normalize(cameraPos - worldPos);

    // Surface-to-light direction supplied by C++.
    // The same vector is also used to build the shadow camera.
    float3 L = normalize(float3(
                directionalLightDirX,
                directionalLightDirY,
                directionalLightDirZ));

    float3 H = normalize(L + V);

    float shadowFactor = ComputeShadowFactor(worldPos, normal, L, isCharacter);

    float lightsEnabled = gSceneLightsEnabled != 0u ? 1.0f : 0.0f;

    float3 finalColor = float3(0.0f, 0.0f, 0.0f);

    float outline = 1.0f;

    if (isCharacter > 0.5f)
    {
        uint normalWidth;
        uint normalHeight;

        g_gbufferNormal.GetDimensions(normalWidth, normalHeight);

        float2 normalTexelSize = float2(
                1.0f / float(normalWidth),
                1.0f / float(normalHeight));

        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            float2 neighborUV = input.uv + outlineOffsets[i] * normalTexelSize;
            float4 neighborNormalSample = g_gbufferNormal.SampleLevel(g_sampler, neighborUV, 0);
            float3 neighborNormal = normalize(neighborNormalSample.xyz * 2.0f - 1.0f);

            float3 neighborPosition = g_gbufferPos.SampleLevel(g_sampler, neighborUV, 0).xyz;

            float deltaNormal = 1.0f - dot(normal, neighborNormal);
            float deltaPosition = length(worldPos - neighborPosition);

            if (deltaNormal > 0.25f || deltaPosition > 0.4f)
            {
                outline = 0.0f;
                break;
            }
        }

        // Ambient/base visibility remains even when J disables direct lights.
        float characterAmbientVisibility = lerp(0.45f, 1.0f, saturate(ao));

        finalColor = albedo * 0.34f * characterAmbientVisibility;

        if (lightsEnabled > 0.5f)
        {
            const float halfLambert = saturate(dot(normal, L) * 0.5f + 0.5f);

            // The previous hard 0.45 threshold produced large polygon-shaped
            // gray regions on the PMX clothing. That is toon-band shading,
            // not shadow acne. Use a narrow smooth ramp instead.
            const float toonRamp = smoothstep(0.36f, 0.58f, halfLambert);
            const float toonBand = lerp( 0.18f, 0.66f, toonRamp);

            // Preserve a small amount of scene-shadow response while avoiding
            // unstable PMX self-shadowing. The character still writes into the
            // shadow map and therefore still casts a shadow onto Sponza.
            const float filteredCharacterShadow = smoothstep(0.10f, 0.90f, shadowFactor);

            const float characterShadowVisibility = lerp(1.0f, filteredCharacterShadow, CHARACTER_SHADOW_MAP_STRENGTH);

            const float toonDirect = toonBand * characterShadowVisibility;

            float rim = 1.0f - saturate(dot(normal, V));
            float rimIntensity = pow(rim, 4.0f) * saturate(dot(normal, L));
            float toonRim = smoothstep(0.25f, 0.45f, rimIntensity) * 0.20f;

            finalColor += albedo * toonDirect + toonRim.xxx;
        }

        finalColor *= outline;
    }
    else
    {
        // Ambient/base visibility is always present. J only disables direct
        // directional/point/specular contributions.
        float ambientVisibility = lerp(0.35f, 1.0f, saturate(ao));
        float ambient = 0.30f * ambientVisibility;

        finalColor = albedo * ambient;

        if (lightsEnabled > 0.5f)
        {
            float directionalNdotL = saturate(dot(normal, L));

            // Only direct light is shadowed. Ambient light is not.
            float directionalDiffuse = directionalNdotL * shadowFactor * 1.10f;
            float directionalSpecular = 0.0f;

            if (directionalNdotL > 0.0f)
            {
                directionalSpecular = pow(saturate(dot(normal, H)), 32.0f) * 0.08f * shadowFactor;
            }

            finalColor += albedo * directionalDiffuse + directionalSpecular.xxx;

            float3 pointVector = pointLightPos - worldPos;
            float pointDistance = length( pointVector);

            if (pointDistance < pointLightRadius)
            {
                float3 pointL = pointVector /max( pointDistance, 0.0001f);
                float3 pointH = normalize(pointL + V);

                float attenuation = pow( saturate(1.0f - pointDistance / pointLightRadius), 2.0f);

                float pointNdotL = saturate(dot(normal, pointL));
                float pointSpecular = pow(saturate(dot(normal, pointH)), 32.0f) * 0.08f;
                float3 pointContribution = (albedo * pointNdotL + pointSpecular.xxx) * pointLightColor * attenuation * 6.0f;

                finalColor += pointContribution;
            }
        }

        finalColor *= exposure;

        // ACES tone mapping.
        finalColor = (finalColor * (2.51f * finalColor + 0.03f)) / (finalColor * (2.43f * finalColor + 0.59f) + 0.14f);
        finalColor = pow(max(finalColor, 0.0f), 1.0f / 2.2f);
    }

    finalColor = saturate(finalColor);

    if (renderMode == 0)
    {
        float dist = length(cameraPos - worldPos) / 100.0f;
        return float4(dist, dist, dist, 1.0f);
    }
    else if (renderMode == 1)
    {
        return float4(normal * 0.5f + 0.5f, 1.0f);
    }
    else if (renderMode == 2)
    {
        return float4(albedo, 1.0f);
    }
    else if (renderMode == 4)
    {
        return float4(ao, ao, ao, 1.0f);
    }
    else if (renderMode == 5)
    {
        // Shadow mode is a visibility/debug view:
        // white = lighting is not shadowed, black = shadowed.
        // When P disables all lights, shadows are not applied, so show white.
        float displayedShadow = lightsEnabled > 0.5f ? shadowFactor : 1.0f;
        return float4(displayedShadow, displayedShadow, displayedShadow, 1.0f);
    }
    return float4(finalColor, 1.0f);
}
