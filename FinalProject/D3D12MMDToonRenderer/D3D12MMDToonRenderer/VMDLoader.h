#pragma once
#include <string>
#include <vector>
#include <map>
#include <DirectXMath.h>

struct VMDBoneKeyframe {
    std::wstring boneName;
    uint32_t frame;
    DirectX::XMFLOAT3 position; // Offset
    DirectX::XMFLOAT4 rotation; // Quaternion
};

class VMDLoader {
public:
    VMDLoader() = default;
    ~VMDLoader() = default;

    bool Load(const std::wstring& filename);

    // 取得分類並排序好的關鍵影格 (Key: 骨骼名稱, Value: 該骨骼的所有影格)
    const std::map<std::wstring, std::vector<VMDBoneKeyframe>>& GetBoneAnimations() const { return m_boneAnimations; }

    uint32_t GetMaxFrame() const { return m_maxFrame; }

private:
    std::map<std::wstring, std::vector<VMDBoneKeyframe>> m_boneAnimations;
    uint32_t m_maxFrame = 0;
};