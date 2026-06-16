#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>

enum AssimpOpacityMode
{
    ASSIMP_OPACITY_OPAQUE = 0,
    ASSIMP_OPACITY_DIFFUSE_ALPHA = 1
};

struct AssimpVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

struct AssimpMesh
{
    UINT startIndex;
    UINT vertexCount;

    std::wstring materialName;

    // True only when map_Kd exists and the referenced file was found.
    bool hasDiffuseTexture;

    // Used when the material has no map_Kd.
    // xyz = Kd, w = material opacity.
    DirectX::XMFLOAT4 diffuseColor;

    // map_Kd. MISSING_TEXTURE means the MTL declared a texture but it
    // could not be resolved or loaded.
    std::wstring diffuseTexturePath;

    // This particular Sponza package has no map_d entries.
    // Only leaf, vase_plant and chain use alpha embedded in map_Kd.
    UINT opacityMode;
};

class AssimpLoader
{
public:
    AssimpLoader() = default;
    ~AssimpLoader() = default;

    bool Load(const std::wstring& filename);

    const std::vector<AssimpVertex>& GetVertices() const { return m_vertices; }
    const std::vector<AssimpMesh>& GetMeshes() const { return m_meshes; }

private:
    std::vector<AssimpVertex> m_vertices;
    std::vector<AssimpMesh> m_meshes;
};
