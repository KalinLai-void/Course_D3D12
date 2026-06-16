//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "Camera.h"
#include "DXSample.h"
#include "MMDAnimator.h"
#include <chrono>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <DescriptorHeap.h>
#include <RenderTargetState.h>
#include <SimpleMath.h>
#include <GraphicsMemory.h>

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloTexture : public DXSample
{
public:
    D3D12HelloTexture(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    std::chrono::high_resolution_clock::time_point m_lastTime;
    float m_deltaTime;
    
    UINT m_renderMode = 3;
    bool m_showCharacter = true;

    // One shared PMX transform for every pass.
    // GeometryPass and ShadowPass must use identical values or the visible
    // character and its shadow will be rendered in different locations.
    DirectX::XMFLOAT4 m_pmxPositionScale =
        DirectX::XMFLOAT4(-400.0f, 0.0f, -100.0f, 12.0f);

    DirectX::XMFLOAT4 m_pmxRotation =
        DirectX::XMFLOAT4(
            0.0f,
            1.57079632679f, // +90 degrees
            0.0f,
            0.0f);

    static const UINT FrameCount = 2;
    static const UINT TextureWidth = 256;
    static const UINT TextureHeight = 256;
    static const UINT TexturePixelSize = 4;    // The number of bytes used to represent a pixel in the texture.

    struct Vertex {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 uv;

        uint32_t boneIndices[4]; // bone ID
        float boneWeights[4];    // bone weights (total must = 1.0)

        // 0.0f = static scene (Sponza), 1.0f = skinned PMX character.
        // Do not infer this from bone weight because some valid PMX vertices
        // may have zero/invalid weights and would otherwise tear the mesh.
        float isCharacter;
    };

    // Textute
    std::vector<ComPtr<ID3D12Resource>> m_textures;
    std::vector<ComPtr<ID3D12Resource>> m_textureUploadHeaps;
    std::vector<std::wstring> m_textureFiles;

    struct MeshData {
        UINT indexCount;
        UINT startIndex;

        // Two consecutive descriptors:
        // +0 = diffuse texture (t0)
        // +1 = opacity texture (t1)
        UINT descriptorBaseIndex;

        // 0 = opaque, 1 = alpha cutout from diffuse alpha.
        UINT opacityMode;

        // False = Sponza/static mesh, true = PMX character mesh.
        bool isCharacterMesh;

        // The uploaded MTL explicitly assigns white.DDS to Material__47.
        bool isSponzaWhiteMaterial;
    };
    std::vector<MeshData> m_meshes;

    struct LightPassConstants {
        UINT renderMode;
        float cameraPosX;
        float cameraPosY;
        float cameraPosZ;

        float exposure;
        float pointLightRadius;
        float directionalLightDirX;
        float padding0;

        float pointLightPosX;
        float pointLightPosY;
        float pointLightPosZ;
        float directionalLightDirY;

        float pointLightColorR;
        float pointLightColorG;
        float pointLightColorB;
        float directionalLightDirZ;

        DirectX::XMFLOAT4X4 lightViewProj;
    };

    static_assert(
        sizeof(LightPassConstants) == 128,
        "LightPassConstants must remain exactly 32 DWORDs.");

    float currentExposure = 1.0f;
    bool m_enableSceneLights = true;

    // Directional-light controls:
    // H / K = horizontal azimuth
    // U / M = vertical elevation
    float m_directionalLightAzimuth = -0.5880026f;
    float m_directionalLightElevation = 0.95f;

    DirectX::XMFLOAT3 m_directionalLightDirection =
        DirectX::XMFLOAT3(0.45f, 0.81f, -0.30f);

    // G-Buffer
    enum GBufferType {
        GBUFFER_ALBEDO = 0,
        GBUFFER_NORMAL,
        GBUFFER_POSITION,
        GBUFFER_COUNT
    };

    ComPtr<ID3D12Resource> m_gBufferTextures[GBUFFER_COUNT];
    ComPtr<ID3D12DescriptorHeap> m_gBufferRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12Resource> m_depthStencil;

    // SSAO: white AO render target
    UINT m_ssaoWidth = 0;
    UINT m_ssaoHeight = 0;

    ComPtr<ID3D12Resource> m_ssaoRawTexture;
    ComPtr<ID3D12DescriptorHeap> m_ssaoRtvHeap;

    static const UINT SSAO_KERNEL_SIZE = 16;

    struct SsaoConstants
    {
        DirectX::XMFLOAT4 samples[SSAO_KERNEL_SIZE];

        DirectX::XMFLOAT4X4 view;
        DirectX::XMFLOAT4X4 projection;

        // x = radius
        // y = bias
        // z = intensity
        // w = enabled
        DirectX::XMFLOAT4 parameters;
    };

    // Render mode 4 always displays SSAO. This flag only controls whether
    // SSAO is applied outside the SSAO debug view (for effect comparison).
    bool m_enableSsao = true;

    float m_ssaoRadius = 0.25f;
    float m_ssaoBias = 0.030f;
    float m_ssaoIntensity = 1.80f;

    SsaoConstants m_ssaoConstants = {};

    ComPtr<ID3D12DescriptorHeap> m_ssaoSrvHeap;
    ComPtr<ID3D12RootSignature> m_ssaoRootSignature;
    ComPtr<ID3D12PipelineState> m_ssaoPipelineState;

    ComPtr<ID3D12Resource> m_ssaoConstantBuffer;
    UINT8* m_pSsaoConstantData = nullptr;

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    UINT m_rtvDescriptorSize;

    ComPtr<ID3D12Resource> m_constantBuffer;
    UINT8* m_pCbvDataBegin;

    // bone
    ComPtr<ID3D12Resource> m_boneConstantBuffer;
    UINT8* m_pBoneDataBegin = nullptr;

    MMDAnimator m_animator;

    // App resources.
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    ComPtr<ID3D12Resource> m_texture;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    // Assimp
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;

    // Lighting Pass
    ComPtr<ID3D12RootSignature> m_lightRootSignature;
    ComPtr<ID3D12PipelineState> m_lightPipelineState;
    ComPtr<ID3D12DescriptorHeap> m_gBufferSrvHeap;

    // UI
    enum UIDescriptor
    {
        UI_FONT = 0,
        UI_DESCRIPTOR_COUNT
    };

    std::unique_ptr<DirectX::GraphicsMemory> m_graphicsMemory;
    std::unique_ptr<DirectX::DescriptorHeap> m_uiDescriptorHeap;
    std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;
    std::unique_ptr<DirectX::SpriteFont> m_spriteFont;

    float m_currentFps = 0.0f;

    void LoadPipeline();
    void LoadAssets();
    void LoadModel(std::wstring filename);

    void PopulateCommandList();
    void WaitForPreviousFrame();

    void CreateGBuffers();
    void CreateSSAOResources();
    void CreateSSAOPipeline();
    void UpdateSSAOConstants();

    void HandleInput();

    // Shadow Map
    UINT m_shadowMapSize = 2048; // ���ѪR�׳��v
    ComPtr<ID3D12Resource> m_shadowTexture;
    ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
    ComPtr<ID3D12RootSignature> m_shadowRootSignature;
    ComPtr<ID3D12PipelineState> m_shadowPipelineState;
    DirectX::XMFLOAT4X4 m_lightViewProj;

    void CreateShadowResources();
    void CreateShadowPipeline();
};
