#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>

struct PMXVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
    uint32_t boneIndices[4];
    float boneWeights[4];
};

struct PMXMaterial {
    std::wstring texturePath;
    UINT indexCount;
};

struct PMXBone {
    std::wstring name;          
    int parentIndex;            
    DirectX::XMFLOAT3 position; 
};

class PMXLoader {
public:
    PMXLoader() = default;
    ~PMXLoader() = default;

    bool Load(const std::wstring& filename);

    const std::vector<PMXVertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_indices; }
    const std::vector<PMXMaterial>& GetMaterials() const { return m_materials; }

    const std::vector<PMXBone>& GetBones() const { return m_bones; }

private:
    std::vector<PMXVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<PMXMaterial> m_materials;

    std::vector<PMXBone> m_bones;
};