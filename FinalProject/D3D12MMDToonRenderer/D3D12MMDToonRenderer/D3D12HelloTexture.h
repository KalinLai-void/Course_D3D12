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
    };

    // Textute
    std::vector<ComPtr<ID3D12Resource>> m_textures;
    std::vector<ComPtr<ID3D12Resource>> m_textureUploadHeaps;
    std::vector<std::wstring> m_textureFiles;

    struct MeshData {
        UINT indexCount;
        UINT startIndex;
        int textureIndex;
    };
    std::vector<MeshData> m_meshes;

    struct LightPassConstants {
        UINT renderMode;
        float cameraPosX;
        float cameraPosY;
        float cameraPosZ;
    };

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

    void LoadPipeline();
    void LoadAssets();
    void LoadModel(std::wstring filename);

    void PopulateCommandList();
    void WaitForPreviousFrame();

    void CreateGBuffers();

    void HandleInput();
};
