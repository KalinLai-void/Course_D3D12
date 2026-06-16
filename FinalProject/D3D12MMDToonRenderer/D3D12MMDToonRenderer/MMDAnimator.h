#pragma once
#include "PMXLoader.h"
#include "VMDLoader.h"
#include <vector>
#include <map>
#include <DirectXMath.h>

class MMDAnimator {
public:
    MMDAnimator() = default;
    ~MMDAnimator() = default;

    // 初始化：將 PMX 骨架與 VMD 動畫對接
    void Initialize(const std::vector<PMXBone>& bones, const std::map<std::wstring, std::vector<VMDBoneKeyframe>>& animations);

    // 每幀更新：推進時間並計算矩陣
    void Update(float deltaTime);

    // 取得算好的 256 個蒙皮矩陣 (可以直接 memcpy 給 GPU)
    const std::vector<DirectX::XMMATRIX>& GetSkinningMatrices() const { return m_skinningMatrices; }

private:
    struct BoneNode {
        int parentIndex;
        DirectX::XMFLOAT3 localBindPosition;      // 該骨頭相對於父骨頭的初始偏移量
        DirectX::XMFLOAT3 inverseBindTranslation; // 轉回世界原點的反向矩陣參數
        std::vector<VMDBoneKeyframe> keyframes;   // 專屬這根骨頭的動畫
    };

    std::vector<BoneNode> m_bones;
    std::vector<DirectX::XMMATRIX> m_skinningMatrices;

    float m_currentTime = 0.0f;
    float m_fps = 30.0f;          // MMD 標準幀率
    uint32_t m_maxFrame = 0;      // 動畫總長度
};