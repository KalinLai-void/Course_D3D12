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

#include "stdafx.h"
#include "D3D12HelloTexture.h"
#include "InputManager.h"
#include "AssimpLoader.h"
#include "PMXLoader.h"
#include "VMDLoader.h"
#include <DDSTextureLoader.h>
#include <locale>
#include <codecvt>
#include <ResourceUploadBatch.h>
#include <WICTextureLoader.h>
#include <algorithm>
#include <cwctype>
#include <cwchar>

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

namespace
{
    float Clamp01(float value)
    {
        if (value < 0.0f)
            return 0.0f;

        if (value > 1.0f)
            return 1.0f;

        return value;
    }

    UINT8 FloatToByte(float value)
    {
        value = Clamp01(value);
        return static_cast<UINT8>(value * 255.0f + 0.5f);
    }

    UINT32 PackRGBA8(const DirectX::XMFLOAT4& color)
    {
        const UINT32 r = FloatToByte(color.x);
        const UINT32 g = FloatToByte(color.y);
        const UINT32 b = FloatToByte(color.z);
        const UINT32 a = FloatToByte(color.w);

        // Memory byte order for DXGI_FORMAT_R8G8B8A8_UNORM:
        // R, G, B, A on little-endian Windows.
        return r | (g << 8) | (b << 16) | (a << 24);
    }

    std::wstring MakeSolidColorToken(const DirectX::XMFLOAT4& color)
    {
        wchar_t buffer[32] = {};
        swprintf_s(buffer, L"SOLID_RGBA8_%08X", PackRGBA8(color));
        return buffer;
    }

    bool TryParseSolidColorToken(
        const std::wstring& token,
        UINT32& pixel)
    {
        const std::wstring prefix = L"SOLID_RGBA8_";
        if (token.compare(0, prefix.size(), prefix) != 0)
            return false;

        const wchar_t* hexText = token.c_str() + prefix.size();
        wchar_t* endPointer = nullptr;
        const unsigned long value = wcstoul(hexText, &endPointer, 16);

        if (endPointer == hexText || *endPointer != L'\0')
            return false;

        pixel = static_cast<UINT32>(value);
        return true;
    }
}

D3D12HelloTexture::D3D12HelloTexture(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0)
{
}

void D3D12HelloTexture::OnInit()
{
    m_lastTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = 0.0f;

    Camera::Get().Initialize(m_aspectRatio);

    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloTexture::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    // DirectXTK12 SpriteBatch allocates transient vertex data through
    // GraphicsMemory. It must exist before the first SpriteBatch::End().
    m_graphicsMemory =
        std::make_unique<DirectX::GraphicsMemory>(
            m_device.Get());

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a shader resource view (SRV) heap for the texture.
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

// Load the sample assets.
void D3D12HelloTexture::LoadAssets()
{
    CreateGBuffers();
    //LoadModel(L"sponza/sponza.obj");

    AssimpLoader assimpLoader;
    if (assimpLoader.Load(L"Assets/Models/sponza/sponza.obj"))
    {
        for (const auto& av : assimpLoader.GetVertices()) {
            Vertex v = {};
            v.position = av.position;
            v.normal = av.normal;
            v.uv = av.uv;
            // Vertex v = {} already clears all bone indices and weights.
            v.isCharacter = 0.0f;
            m_vertices.push_back(v);
        }

        UINT currentVertexOffset = 0;
        for (const auto& am : assimpLoader.GetMeshes()) {
            MeshData mesh = {};
            mesh.startIndex = currentVertexOffset;
            mesh.indexCount = am.vertexCount;
            mesh.opacityMode = am.opacityMode;
            mesh.isCharacterMesh = false;
            mesh.isSponzaWhiteMaterial =
                (am.materialName == L"Material__47");

            // Each material occupies two consecutive SRV descriptors.
            mesh.descriptorBaseIndex =
                static_cast<UINT>(m_textureFiles.size());

            // t0: map_Kd when available. A material that legitimately has
            // no texture uses its MTL Kd color as a generated 1x1 texture.
            // MISSING_TEXTURE remains magenta so a broken map_Kd is obvious.
            if (am.hasDiffuseTexture)
            {
                m_textureFiles.push_back(am.diffuseTexturePath);
            }
            else
            {
                m_textureFiles.push_back(
                    MakeSolidColorToken(am.diffuseColor));
            }

            // This Sponza package has no map_d entries. Keep t1 bound to a
            // valid white texture; alpha-cutout materials read diffuse alpha.
            m_textureFiles.push_back(L"DEFAULT_WHITE_TEXTURE");

            m_meshes.push_back(mesh);
            currentVertexOffset += am.vertexCount;
        }
    }

    PMXLoader pmxLoader;
    if (pmxLoader.Load(L"Assets/Models/MMD/KizunaAI_ver1.01/kizunaai/kizunaai.pmx"))
    {
        UINT currentVertexOffset = static_cast<UINT>(m_vertices.size());

        const auto& pmxVerts = pmxLoader.GetVertices();
        const auto& pmxIndices = pmxLoader.GetIndices();

        for (uint32_t idx : pmxIndices) {
            const auto& pv = pmxVerts[idx];
            Vertex v = {};

            v.position = pv.position;
            v.normal = pv.normal;
            v.uv = pv.uv;

            memcpy(v.boneIndices, pv.boneIndices, sizeof(uint32_t) * 4);
            memcpy(v.boneWeights, pv.boneWeights, sizeof(float) * 4);
            v.isCharacter = 1.0f;

            m_vertices.push_back(v);
        }

        // 處理 PMX 材質
        for (const auto& mat : pmxLoader.GetMaterials()) {
            MeshData mesh = {};
            mesh.startIndex = currentVertexOffset;
            mesh.indexCount = mat.indexCount; // 因為攤平了，Index 數量就是 Vertex 數量
            mesh.opacityMode = ASSIMP_OPACITY_DIFFUSE_ALPHA;
            mesh.isCharacterMesh = true;
            mesh.isSponzaWhiteMaterial = false;

            mesh.descriptorBaseIndex =
                static_cast<UINT>(m_textureFiles.size());

            // t0: PMX diffuse texture
            m_textureFiles.push_back(mat.texturePath);

            // t1: PMX has no separate map_d in this loader.
            // Use a 1x1 white opacity texture.
            m_textureFiles.push_back(L"DEFAULT_WHITE_TEXTURE");

            m_meshes.push_back(mesh);
            currentVertexOffset += mat.indexCount;
        }
    }

    VMDLoader vmdLoader;
    if (vmdLoader.Load(L"Assets/Models/MMD/Anims/rubychan_no_mouth.vmd"))
    {
        m_animator.Initialize(pmxLoader.GetBones(), vmdLoader.GetBoneAnimations());
    }

    // Create the root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];

        // One table containing two consecutive SRVs:
        // t0 = diffuse, t1 = opacity.
        ranges[0].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            2,
            0,
            0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParameters[4];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[2].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

        // b2: per-material opacity mode used by the pixel shader.
        rootParameters[3].InitAsConstants(
            1,
            2,
            0,
            D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_ANISOTROPIC;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 8;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vsMain, psMain, errBlob;
        HRESULT hrVS = D3DCompileFromFile(L"GeometryPass.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsMain, &errBlob);
        if (FAILED(hrVS)) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            ThrowIfFailed(hrVS);
        }

        HRESULT hrPS = D3DCompileFromFile(L"GeometryPass.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psMain, &errBlob);
        if (FAILED(hrPS)) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            ThrowIfFailed(hrPS);
        }

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

            { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     1, DXGI_FORMAT_R32_FLOAT,          0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vsMain->GetBufferPointer(), vsMain->GetBufferSize());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(psMain->GetBufferPointer(), psMain->GetBufferSize()); 
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 3;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Albedo
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Normal
        psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT; // Position
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // Create the vertex buffer.
    {
        /*if (m_vertices.empty()) {
            MessageBoxA(nullptr, "Model load failed! Vertex count is 0", "Error", MB_OK);
            return;
        }*/

        // BufferSize = Vertex count * sizeof(single vertex)
        const UINT vertexBufferSize = static_cast<UINT>(m_vertices.size() * sizeof(Vertex));

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        // Copy the model data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));

        memcpy(pVertexDataBegin, m_vertices.data(), vertexBufferSize);

        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_constantBuffer)));

    ThrowIfFailed(m_constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pCbvDataBegin)));

    const UINT boneBufferSize = sizeof(DirectX::XMMATRIX) * 1024;
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(boneBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_boneConstantBuffer)));

    ThrowIfFailed(m_boneConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pBoneDataBegin)));

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ComPtr<ID3D12Resource> textureUploadHeap;

    // Create material textures.
    {
        // WICTextureLoader requires COM.
        const HRESULT comResult =
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        // RPC_E_CHANGED_MODE only means COM was initialized earlier using
        // another apartment model; WIC can still be used by the application.
        if (FAILED(comResult) &&
            comResult != RPC_E_CHANGED_MODE)
        {
            ThrowIfFailed(comResult);
        }

        ResourceUploadBatch resourceUpload(m_device.Get());
        resourceUpload.Begin();

        // m_textureFiles is arranged as pairs:
        // [diffuse0, opacity0, diffuse1, opacity1, ...]
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors =
            static_cast<UINT>(m_textureFiles.size());
        srvHeapDesc.Type =
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags =
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        ThrowIfFailed(
            m_device->CreateDescriptorHeap(
                &srvHeapDesc,
                IID_PPV_ARGS(&m_srvHeap)));

        const UINT srvSize =
            m_device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        static const UINT32 whitePixel =
            0xFFFFFFFFu;

        // Little-endian RGBA8: FF 00 FF FF = magenta.
        static const UINT32 missingDiffusePixel =
            0xFFFF00FFu;

        auto CreateSolidTexture =
            [&](const UINT32* pixel,
                ComPtr<ID3D12Resource>& textureResource)
        {
            D3D12_RESOURCE_DESC textureDesc =
                CD3DX12_RESOURCE_DESC::Tex2D(
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    1,
                    1,
                    1,
                    1);

            ThrowIfFailed(
                m_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES(
                        D3D12_HEAP_TYPE_DEFAULT),
                    D3D12_HEAP_FLAG_NONE,
                    &textureDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(&textureResource)));

            D3D12_SUBRESOURCE_DATA data = {};
            data.pData = pixel;
            data.RowPitch = sizeof(UINT32);
            data.SlicePitch = sizeof(UINT32);

            resourceUpload.Upload(
                textureResource.Get(),
                0,
                &data,
                1);

            resourceUpload.Transition(
                textureResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        };

        for (UINT i = 0;
             i < static_cast<UINT>(m_textureFiles.size());
             ++i)
        {
            ComPtr<ID3D12Resource> textureResource;
            HRESULT hr = E_FAIL;

            const std::wstring path = m_textureFiles[i];
            const bool isOpacityDescriptor = (i % 2u) == 1u;

            UINT32 generatedPixel = 0;

            if (TryParseSolidColorToken(path, generatedPixel))
            {
                CreateSolidTexture(
                    &generatedPixel,
                    textureResource);

                hr = S_OK;
            }
            else if (path == L"DEFAULT_WHITE_TEXTURE")
            {
                CreateSolidTexture(
                    &whitePixel,
                    textureResource);

                hr = S_OK;
            }
            else if (path != L"MISSING_TEXTURE")
            {
                std::wstring lowerPath = path;

                std::transform(
                    lowerPath.begin(),
                    lowerPath.end(),
                    lowerPath.begin(),
                    [](wchar_t c)
                    {
                        return static_cast<wchar_t>(
                            std::towlower(c));
                    });

                const bool isDDS =
                    lowerPath.size() >= 4 &&
                    lowerPath.substr(lowerPath.size() - 4) ==
                        L".dds";

                if (isDDS)
                {
                    std::unique_ptr<uint8_t[]> ddsData;
                    std::vector<D3D12_SUBRESOURCE_DATA>
                        subresources;

                    hr = DirectX::LoadDDSTextureFromFile(
                        m_device.Get(),
                        path.c_str(),
                        &textureResource,
                        ddsData,
                        subresources);

                    if (SUCCEEDED(hr))
                    {
                        resourceUpload.Upload(
                            textureResource.Get(),
                            0,
                            subresources.data(),
                            static_cast<UINT>(
                                subresources.size()));

                        resourceUpload.Transition(
                            textureResource.Get(),
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    }
                }
                else
                {
                    hr = DirectX::CreateWICTextureFromFile(
                        m_device.Get(),
                        resourceUpload,
                        path.c_str(),
                        &textureResource);
                }
            }

            if (FAILED(hr))
            {
                OutputDebugStringW(
                    (std::wstring(
                         isOpacityDescriptor
                             ? L"[Warning] Opacity texture load failed: "
                             : L"[Warning] Diffuse texture load failed: ") +
                     path +
                     L"\n").c_str());

                textureResource.Reset();

                // Missing opacity remains fully visible so the material does
                // not disappear. Missing diffuse is shown in magenta.
                CreateSolidTexture(
                    isOpacityDescriptor
                        ? &whitePixel
                        : &missingDiffusePixel,
                    textureResource);
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format =
                textureResource->GetDesc().Format;
            srvDesc.ViewDimension =
                D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels =
                textureResource->GetDesc().MipLevels;

            CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor(
                m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
                i,
                srvSize);

            m_device->CreateShaderResourceView(
                textureResource.Get(),
                &srvDesc,
                descriptor);

            m_textures.push_back(textureResource);
        }

        auto uploadFinished =
            resourceUpload.End(m_commandQueue.Get());

        uploadFinished.wait();
    }

    // Lighting Pass
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 3;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_gBufferSrvHeap)));

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_gBufferSrvHeap->GetCPUDescriptorHandleForHeapStart());
        UINT cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        DXGI_FORMAT gbufferFormats[3] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT };
        for (int i = 0; i < 3; i++) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = gbufferFormats[i];
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(m_gBufferTextures[i].Get(), &srvDesc, srvHandle);
            srvHandle.Offset(1, cbvSrvDescriptorSize);
        }

        // create root signature for light pass
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_DESCRIPTOR_RANGE1 rangesLight[1];
        rangesLight[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParametersLight[2];
        rootParametersLight[0].InitAsDescriptorTable(1, &rangesLight[0], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParametersLight[1].InitAsConstants(4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC lightRootSigDesc;
        lightRootSigDesc.Init_1_1(2, rootParametersLight, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> lightSignature, lightError;
        D3DX12SerializeVersionedRootSignature(&lightRootSigDesc, featureData.HighestVersion, &lightSignature, &lightError);
        m_device->CreateRootSignature(0, lightSignature->GetBufferPointer(), lightSignature->GetBufferSize(), IID_PPV_ARGS(&m_lightRootSignature));

        // create Pipeline State (PSO) for light pass ---
        ComPtr<ID3DBlob> vsLight, psLight, errBlob;

        HRESULT hrVSLight = D3DCompileFromFile(L"LightingPass.hlsl", nullptr, nullptr, "VSLighting", "vs_5_0", 0, 0, &vsLight, &errBlob);
        if (FAILED(hrVSLight)) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            ThrowIfFailed(hrVSLight);
        }

        HRESULT hrPSLight = D3DCompileFromFile(L"LightingPass.hlsl", nullptr, nullptr, "PSLighting", "ps_5_0", 0, 0, &psLight, &errBlob);
        if (FAILED(hrPSLight)) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            ThrowIfFailed(hrPSLight);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC lightPsoDesc = {};
        lightPsoDesc.pRootSignature = m_lightRootSignature.Get();
        lightPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vsLight->GetBufferPointer(), vsLight->GetBufferSize());
        lightPsoDesc.PS = CD3DX12_SHADER_BYTECODE(psLight->GetBufferPointer(), psLight->GetBufferSize());
        lightPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        lightPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        lightPsoDesc.DepthStencilState.DepthEnable = FALSE; // because is for screen
        lightPsoDesc.DepthStencilState.StencilEnable = FALSE;
        lightPsoDesc.SampleMask = UINT_MAX;
        lightPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        lightPsoDesc.NumRenderTargets = 1; // for screen, so render only once
        lightPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        lightPsoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&lightPsoDesc, IID_PPV_ARGS(&m_lightPipelineState)));
    }

    // Close the command list and execute it to begin the initial GPU setup.
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }

    // ==========================================
    // Create HUD / text resources
    // ==========================================
    {
        m_uiDescriptorHeap =
            std::make_unique<DirectX::DescriptorHeap>(
                m_device.Get(),
                UI_DESCRIPTOR_COUNT
            );

        DirectX::ResourceUploadBatch upload(
            m_device.Get()
        );

        upload.Begin();

        // The HUD is drawn after lighting with no DSV bound.
        DirectX::RenderTargetState renderTargetState(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_UNKNOWN
        );

        DirectX::SpriteBatchPipelineStateDescription
            spritePipeline(renderTargetState);

        m_spriteBatch =
            std::make_unique<DirectX::SpriteBatch>(
                m_device.Get(),
                upload,
                spritePipeline
            );

        m_spriteFont =
            std::make_unique<DirectX::SpriteFont>(
                m_device.Get(),
                upload,
                L"Assets/Debug.spritefont",
                m_uiDescriptorHeap->GetCpuHandle(UI_FONT),
                m_uiDescriptorHeap->GetGpuHandle(UI_FONT)
            );

        auto finished =
            upload.End(m_commandQueue.Get());

        finished.wait();

        m_spriteBatch->SetViewport(m_viewport);
    }
}

//void D3D12HelloTexture::LoadModel(std::wstring filename)
//{
//    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
//    std::string narrowFilename = converter.to_bytes(filename);
//
//    Assimp::Importer importer;
//
//    const aiScene* scene = importer.ReadFile(narrowFilename,
//        aiProcess_Triangulate |
//        aiProcess_FlipUVs |
//        aiProcess_GenNormals |
//        aiProcess_ConvertToLeftHanded); // DirectX using Left Hand Coordinate System
//
//    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
//        OutputDebugStringA(importer.GetErrorString());
//        MessageBoxW(nullptr, L"Can't found the model!", L"Error", MB_OK);
//        return;
//    }
//
//    UINT currentVertexOffset = 0;
//    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
//        aiMesh* mesh = scene->mMeshes[m];
//
//        MeshData data = {};
//        data.startIndex = currentVertexOffset;
//        data.indexCount = mesh->mNumFaces * 3; // Triangulate
//        data.textureIndex = mesh->mMaterialIndex; // using Material ID as Texture index
//        m_meshes.push_back(data);
//
//        // Using the order of faces' index, let vertexs flatten and save to array
//        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
//            const aiFace& face = mesh->mFaces[i];
//
//            for (unsigned int j = 0; j < face.mNumIndices; j++) {
//                unsigned int idx = face.mIndices[j];
//
//                Vertex vertex = {};
//
//                vertex.position.x = mesh->mVertices[idx].x;
//                vertex.position.y = mesh->mVertices[idx].y;
//                vertex.position.z = mesh->mVertices[idx].z;
//
//                if (mesh->HasNormals()) {
//                    vertex.normal.x = mesh->mNormals[idx].x;
//                    vertex.normal.y = mesh->mNormals[idx].y;
//                    vertex.normal.z = mesh->mNormals[idx].z;
//                }
//
//                if (mesh->HasTextureCoords(0)) {
//                    vertex.uv.x = mesh->mTextureCoords[0][idx].x;
//                    vertex.uv.y = mesh->mTextureCoords[0][idx].y;
//                }
//
//                m_vertices.push_back(vertex);
//            }
//        }
//        currentVertexOffset += data.indexCount;
//    }
//
//    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
//        aiMaterial* material = scene->mMaterials[i];
//        aiString path;
//
//        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS) {
//            std::string rawPath = path.C_Str();
//
//            // handle file's format (only get DDS files)
//            size_t lastDot = rawPath.find_last_of(".");
//            if (lastDot != std::string::npos) {
//                rawPath = rawPath.substr(0, lastDot) + ".dds";
//            }
//
//            size_t lastSlash = rawPath.find_last_of("\\/");
//            std::string fileName = (lastSlash == std::string::npos) ? rawPath : rawPath.substr(lastSlash + 1);
//            std::wstring wPath = L"sponza/" + converter.from_bytes(fileName);
//
//            OutputDebugStringW((L"Loading Textures: " + wPath + L"\n").c_str());
//
//            m_textureFiles.push_back(wPath);
//        }
//        else {
//            m_textureFiles.push_back(L"MISSING_TEXTURE");
//        }
//    }
//}

// Update frame-based values.
void D3D12HelloTexture::OnUpdate()
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration<float>(currentTime - m_lastTime).count();
    m_lastTime = currentTime;

    HandleInput();

    XMMATRIX modelMatrix = XMMatrixScaling(0.1f, 0.1f, 0.1f);
    XMMATRIX viewMatrix = Camera::Get().GetViewMatrix();
    XMMATRIX projectionMatrix = Camera::Get().GetProjectionMatrix();
    struct ConstantBufferData {
        XMMATRIX model;
        XMMATRIX mvp;

        // xyz = PMX translation in model space, w = PMX scale.
        XMFLOAT4 pmxPositionScale;

        // xyz = PMX Euler rotation in radians.
        XMFLOAT4 pmxRotation;
    };

    ConstantBufferData cbData = {};
    cbData.model = XMMatrixTranspose(modelMatrix);
    cbData.mvp = XMMatrixTranspose(modelMatrix * viewMatrix * projectionMatrix);

    // IMPORTANT:
    // The previous package still used scale 12.0 in HLSL, placing the camera
    // inside the PMX model. Start at 1.0 and adjust gradually.
    // Preserve the original PMX object transform.
    cbData.pmxPositionScale = XMFLOAT4(
        0.0f,    // offset X
        0.0f,    // offset Y
        0.0f,    // offset Z
        12.0f);  // original PMX scale

    cbData.pmxRotation = XMFLOAT4(
        0.0f,
        XMConvertToRadians(-90.0f), // original PMX Y rotation
        0.0f,
        0.0f);

    memcpy(m_pCbvDataBegin, &cbData, sizeof(ConstantBufferData));
    
    // MMD Animator
    m_animator.Update(m_deltaTime);

    if (m_pBoneDataBegin != nullptr)
    {
        const auto& skinningMatrices = m_animator.GetSkinningMatrices();
        memcpy(m_pBoneDataBegin, skinningMatrices.data(), sizeof(DirectX::XMMATRIX) * 1024);
    }

    m_currentFps =
        (m_deltaTime > 0.0f)
        ? (1.0f / m_deltaTime)
        : 0.0f;

    InputManager::Get().EndFrame();
}

// Render the scene.
void D3D12HelloTexture::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    // Tell DirectXTK12 that this frame's transient SpriteBatch memory
    // has been submitted to the GPU.
    if (m_graphicsMemory)
    {
        m_graphicsMemory->Commit(
            m_commandQueue.Get());
    }

    WaitForPreviousFrame();
}

void D3D12HelloTexture::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

void D3D12HelloTexture::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    // Set necessary state.
    // ==========================================
    // Pass 1: Geometry Pass (draw model to G-Buffer)
    // ==========================================
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // binding MVP matrix to camera
    m_commandList->SetGraphicsRootConstantBufferView(1, m_constantBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(2, m_boneConstantBuffer->GetGPUVirtualAddress());

    // binding basic texture heaps
    ID3D12DescriptorHeap* geoHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(geoHeaps), geoHeaps);
    m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    CD3DX12_CPU_DESCRIPTOR_HANDLE gBufferRtvHandle(m_gBufferRtvHeap->GetCPUDescriptorHandleForHeapStart());
    UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_commandList->OMSetRenderTargets(GBUFFER_COUNT, &gBufferRtvHandle, TRUE, &dsvHandle);

    // Alpha is also used as a validity flag.  A cleared Position.a of 0 means
    // "no geometry was written here".  This is important after alpha clipping.
    const float clearAlbedo[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float clearNormal[4]  = { 0.5f, 0.5f, 1.0f, 0.0f };
    const float clearPosition[4]= { 0.0f, 0.0f, 0.0f, 0.0f };
    const float* clearColors[GBUFFER_COUNT] =
    {
        clearAlbedo,
        clearNormal,
        clearPosition
    };

    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(gBufferRtvHandle, i, rtvDescriptorSize);
        m_commandList->ClearRenderTargetView(handle, clearColors[i], 0, nullptr);
    }

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

    UINT srvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Draw each mesh using a two-SRV material table.
    for (const auto& mesh : m_meshes)
    {
        if (mesh.isCharacterMesh && !m_showCharacter)
            continue;

        if (mesh.isSponzaWhiteMaterial)
        {
            continue;
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE materialHandle(
            m_srvHeap->GetGPUDescriptorHandleForHeapStart(),
            mesh.descriptorBaseIndex,
            srvSize);

        // materialHandle + 0 -> t0 diffuse
        // materialHandle + 1 -> t1 opacity
        m_commandList->SetGraphicsRootDescriptorTable(
            0,
            materialHandle);

        m_commandList->SetGraphicsRoot32BitConstant(
            3,
            mesh.opacityMode,
            0);

        m_commandList->DrawInstanced(
            mesh.indexCount,
            1,
            mesh.startIndex,
            0);
    }


    // ==========================================
    // Resource  Barrier
    // ==========================================
    D3D12_RESOURCE_BARRIER barriers[3];
    for (int i = 0; i < 3; i++) {
        // let G-Buffer from write (RenderTarget) to read (PixelShaderResource)
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(m_gBufferTextures[i].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    m_commandList->ResourceBarrier(3, barriers);

    // let screen (BackBuffer) as Render Target
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // ==========================================
    // Pass 2: Lighting Pass (from G-Buffer caluate Light to screen)
    // ==========================================
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    const float clearColorBlue[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColorBlue, 0, nullptr);

    // apply Lighting Pipeline & Shader
    m_commandList->SetPipelineState(m_lightPipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_lightRootSignature.Get());

    // SRV Heap in G-buffer to Shader
    ID3D12DescriptorHeap* lightHeaps[] = { m_gBufferSrvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(lightHeaps), lightHeaps);
    m_commandList->SetGraphicsRootDescriptorTable(0, m_gBufferSrvHeap->GetGPUDescriptorHandleForHeapStart());
    
    LightPassConstants lightConstants;
    lightConstants.renderMode = m_renderMode;

    XMFLOAT3 camPos = Camera::Get().GetPosition();
    lightConstants.cameraPosX = camPos.x;
    lightConstants.cameraPosY = camPos.y;
    lightConstants.cameraPosZ = camPos.z;

    // passing renderMode & camera position
    m_commandList->SetGraphicsRoot32BitConstants(1, 4, &lightConstants, 0);

    // draw big trangle above full-screen，trigger Pixel Shader's lighting
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // ==========================================
// HUD / Text Overlay
// 必須放在 Back Buffer 仍為 RENDER_TARGET 時
// ==========================================
    {
        wchar_t overlayText[256] = {};

        const wchar_t* modeName = L"Unknown";

        switch (m_renderMode)
        {
        case 0:
            modeName = L"Depth";
            break;

        case 1:
            modeName = L"Normal";
            break;

        case 2:
            modeName = L"Albedo";
            break;

        case 3:
            modeName = L"Final Color";
            break;
        }

        swprintf_s(
            overlayText,
            _countof(overlayText),
            L"FPS: %.0f\n"
            L"Render Mode: %ls\n"
            L"PMX: %ls",
            m_currentFps,
            modeName,
            m_showCharacter
                ? L"ON"
                : L"OFF"
        );

        ID3D12DescriptorHeap* uiHeaps[] =
        {
            m_uiDescriptorHeap->Heap()
        };

        m_commandList->SetDescriptorHeaps(
            _countof(uiHeaps),
            uiHeaps
        );

        m_spriteBatch->Begin(
            m_commandList.Get()
        );

        const DirectX::SimpleMath::Vector2
            textPosition(16.0f, 16.0f);

        // 黑色陰影
        m_spriteFont->DrawString(
            m_spriteBatch.get(),
            overlayText,
            textPosition +
            DirectX::SimpleMath::Vector2(
                2.0f,
                2.0f
            ),
            DirectX::Colors::Black
        );

        // 白色文字
        m_spriteFont->DrawString(
            m_spriteBatch.get(),
            overlayText,
            textPosition,
            DirectX::Colors::White
        );

        m_spriteBatch->End();
    }

    // ==========================================
    // transfer resources to original
    // ==========================================
    for (int i = 0; i < 3; i++) {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(m_gBufferTextures[i].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    m_commandList->ResourceBarrier(3, barriers);

    // let BackBuffer to PRESENT
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTexture::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12HelloTexture::CreateGBuffers() {
    DXGI_FORMAT formats[GBUFFER_COUNT] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,     // Albedo
        DXGI_FORMAT_R16G16B16A16_FLOAT, // Normal
        DXGI_FORMAT_R32G32B32A32_FLOAT  // World Position
    };

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_gBufferRtvHeap)));

    UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_gBufferRtvHeap->GetCPUDescriptorHandleForHeapStart());

    const float optimizedClearValues[GBUFFER_COUNT][4] =
    {
        { 0.0f, 0.0f, 0.0f, 0.0f }, // Albedo
        { 0.5f, 0.5f, 1.0f, 0.0f }, // Encoded normal, invalid pixel
        { 0.0f, 0.0f, 0.0f, 0.0f }  // World position, invalid pixel
    };

    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            formats[i], m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = formats[i];
        clearValue.Color[0] = optimizedClearValues[i][0];
        clearValue.Color[1] = optimizedClearValues[i][1];
        clearValue.Color[2] = optimizedClearValues[i][2];
        clearValue.Color[3] = optimizedClearValues[i][3];

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&m_gBufferTextures[i])));
        m_device->CreateRenderTargetView(m_gBufferTextures[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    // 👇 以下是新增的 Depth Buffer 建立程式碼 👇
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&m_depthStencil)));

    m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void D3D12HelloTexture::HandleInput()
{
    auto& input = InputManager::Get();

    if (input.IsKeyJustPressed(VK_ESCAPE)) {
        Camera::Get().m_isMouseLocked = false;
    }

    if (!Camera::Get().m_isMouseLocked && input.IsKeyDown(VK_LBUTTON)) {
        Camera::Get().m_isMouseLocked = true;
    }

    Camera::Get().UpdateMouseLock();

    if (!Camera::Get().m_isMouseLocked) {
        if (input.IsKeyDown(VK_RBUTTON)) {
            Camera::Get().Pan(input.GetMouseDeltaX(), input.GetMouseDeltaY());
            //Camera::Get().Update(m_deltaTime);
        }
    }
    else {
        if (input.IsKeyDown(VK_LBUTTON)) {
        }
        else {
            Camera::Get().Update(m_deltaTime);
        }
    }

    if (input.IsKeyJustPressed('Z')) {
        m_renderMode = (m_renderMode + 1) % 4;
    }

    // Diagnostic toggle: X hides/shows only the PMX character.
    // If the large white object disappears, it was the PMX model, not Sponza.
    if (input.IsKeyJustPressed('X')) {
        m_showCharacter = !m_showCharacter;
    }
}
