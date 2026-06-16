#include "stdafx.h"
#include "MMDAnimator.h"

using namespace DirectX;

void MMDAnimator::Initialize(const std::vector<PMXBone>& bones, const std::map<std::wstring, std::vector<VMDBoneKeyframe>>& animations)
{
    m_bones.clear();
    m_bones.reserve(bones.size());
    m_skinningMatrices.resize(1024, XMMatrixIdentity()); // 保證輸出 256 個矩陣
    m_maxFrame = 0;

    for (size_t i = 0; i < bones.size(); ++i) {
        BoneNode node = {};
        node.parentIndex = bones[i].parentIndex;

        // 1. 計算 Inverse Bind Pose (用於將頂點從模型空間拉回原點)
        node.inverseBindTranslation = XMFLOAT3(-bones[i].position.x, -bones[i].position.y, -bones[i].position.z);

        // 2. 計算 Local Bind Position (相對於父節點的初始偏移)
        if (node.parentIndex >= 0 && node.parentIndex < (int)bones.size()) {
            node.localBindPosition.x = bones[i].position.x - bones[node.parentIndex].position.x;
            node.localBindPosition.y = bones[i].position.y - bones[node.parentIndex].position.y;
            node.localBindPosition.z = bones[i].position.z - bones[node.parentIndex].position.z;
        }
        else {
            node.localBindPosition = bones[i].position;
        }

        // 3. 從 VMD 中尋找對應名稱的動畫影格
        auto it = animations.find(bones[i].name);
        if (it != animations.end()) {
            node.keyframes = it->second;
            if (!node.keyframes.empty()) {
                uint32_t lastFrame = node.keyframes.back().frame;
                if (lastFrame > m_maxFrame) m_maxFrame = lastFrame;
            }
        }

        m_bones.push_back(node);
    }
}

void MMDAnimator::Update(float deltaTime)
{
    if (m_bones.empty()) return;

    // 推進時間 (循環播放)
    m_currentTime += deltaTime * m_fps;
    if (m_currentTime > (float)m_maxFrame) {
        m_currentTime = 0.0f;
    }

    std::vector<XMMATRIX> globalMatrices(m_bones.size());

    // 逐一計算每根骨頭的變換矩陣
    for (size_t i = 0; i < m_bones.size(); ++i) {
        const auto& bone = m_bones[i];

        XMVECTOR animPos = XMVectorZero();
        XMVECTOR animRot = XMQuaternionIdentity();

        // --- 尋找關鍵影格並進行插值 ---
        if (!bone.keyframes.empty()) {
            if (m_currentTime <= bone.keyframes.front().frame) {
                animPos = XMLoadFloat3(&bone.keyframes.front().position);
                animRot = XMLoadFloat4(&bone.keyframes.front().rotation);
            }
            else if (m_currentTime >= bone.keyframes.back().frame) {
                animPos = XMLoadFloat3(&bone.keyframes.back().position);
                animRot = XMLoadFloat4(&bone.keyframes.back().rotation);
            }
            else {
                // 線性搜尋目前的區間 (因為影格不多，這裡用迴圈即可)
                for (size_t k = 0; k < bone.keyframes.size() - 1; ++k) {
                    const auto& k0 = bone.keyframes[k];
                    const auto& k1 = bone.keyframes[k + 1];

                    if (m_currentTime >= k0.frame && m_currentTime <= k1.frame) {

                        float t = 0.0f;
                        // 🚨 防護 1：絕對防止除以零！
                        if (k1.frame > k0.frame) {
                            t = (m_currentTime - k0.frame) / (float)(k1.frame - k0.frame);
                        }

                        XMVECTOR p0 = XMLoadFloat3(&k0.position);
                        XMVECTOR p1 = XMLoadFloat3(&k1.position);
                        animPos = XMVectorLerp(p0, p1, t);

                        // 🚨 防護 2：強制正規化 (Normalize)，防止浮點數誤差導致 Slerp 吐出 NaN
                        XMVECTOR r0 = XMQuaternionNormalize(XMLoadFloat4(&k0.rotation));
                        XMVECTOR r1 = XMQuaternionNormalize(XMLoadFloat4(&k1.rotation));
                        animRot = XMQuaternionSlerp(r0, r1, t);
                        break;
                    }
                }
            }
        }

        // --- 計算 Local Matrix ---
        XMVECTOR localBindPos = XMLoadFloat3(&bone.localBindPosition);
        XMVECTOR finalLocalPos = XMVectorAdd(localBindPos, animPos); // 初始偏移 + 動畫偏移

        XMMATRIX localMatrix = XMMatrixAffineTransformation(
            XMVectorSet(1, 1, 1, 0), XMVectorZero(), animRot, finalLocalPos
        );

        // --- 計算 Global Matrix (乘上父節點) ---
        if (bone.parentIndex >= 0 && bone.parentIndex < (int)i) {
            // DirectX 數學是 Row-Major，乘法順序：Local * ParentGlobal
            globalMatrices[i] = XMMatrixMultiply(localMatrix, globalMatrices[bone.parentIndex]);
        }
        else {
            globalMatrices[i] = localMatrix;
        }

        // --- 計算最終送給 GPU 的 Skinning Matrix ---
        // Skinning Matrix = InverseBindPose * GlobalAnimated
        XMMATRIX invBind = XMMatrixTranslation(bone.inverseBindTranslation.x, bone.inverseBindTranslation.y, bone.inverseBindTranslation.z);

        if (i < 1024) {
            // 🚨 HLSL Shader 的 mul(v, M) 需要轉置矩陣 (Transpose)
            m_skinningMatrices[i] = XMMatrixTranspose(XMMatrixMultiply(invBind, globalMatrices[i]));
        }
    }
}