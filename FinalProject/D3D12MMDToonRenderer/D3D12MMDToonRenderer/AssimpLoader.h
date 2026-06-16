#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>

struct AssimpVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

struct AssimpMesh {
    UINT startIndex;
    UINT vertexCount;
    std::wstring texturePath;
};

class AssimpLoader {
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