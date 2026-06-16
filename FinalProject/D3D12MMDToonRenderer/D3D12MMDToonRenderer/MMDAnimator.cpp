#include "stdafx.h"
#include "MMDAnimator.h"

#include <functional>
#include <vector>

using namespace DirectX;

void MMDAnimator::Initialize(
    const std::vector<PMXBone>& bones,
    const std::map<std::wstring, std::vector<VMDBoneKeyframe>>& animations)
{
    m_bones.clear();
    m_bones.reserve(bones.size());
    m_skinningMatrices.assign(1024, XMMatrixIdentity());
    m_maxFrame = 0;

    for (size_t i = 0; i < bones.size(); ++i)
    {
        BoneNode node = {};
        node.parentIndex = bones[i].parentIndex;

        node.inverseBindTranslation = XMFLOAT3(
            -bones[i].position.x,
            -bones[i].position.y,
            -bones[i].position.z);

        if (node.parentIndex >= 0 &&
            node.parentIndex < static_cast<int>(bones.size()))
        {
            node.localBindPosition.x =
                bones[i].position.x - bones[node.parentIndex].position.x;
            node.localBindPosition.y =
                bones[i].position.y - bones[node.parentIndex].position.y;
            node.localBindPosition.z =
                bones[i].position.z - bones[node.parentIndex].position.z;
        }
        else
        {
            node.localBindPosition = bones[i].position;
        }

        const auto animationIt = animations.find(bones[i].name);
        if (animationIt != animations.end())
        {
            node.keyframes = animationIt->second;
            if (!node.keyframes.empty())
            {
                const uint32_t lastFrame = node.keyframes.back().frame;
                if (lastFrame > m_maxFrame)
                    m_maxFrame = lastFrame;
            }
        }

        m_bones.push_back(node);
    }
}

void MMDAnimator::Update(float deltaTime)
{
    if (m_bones.empty())
        return;

    m_currentTime += deltaTime * m_fps;
    if (m_maxFrame > 0 && m_currentTime > static_cast<float>(m_maxFrame))
        m_currentTime = 0.0f;

    const size_t boneCount = m_bones.size();

    std::vector<XMMATRIX> localMatrices(boneCount, XMMatrixIdentity());
    std::vector<XMMATRIX> globalMatrices(boneCount, XMMatrixIdentity());
    std::vector<unsigned char> resolveState(boneCount, 0);

    // First build every local transform.  Do not assume that a parent bone is
    // stored before its child in the PMX file.
    for (size_t i = 0; i < boneCount; ++i)
    {
        const BoneNode& bone = m_bones[i];

        XMVECTOR animPos = XMVectorZero();
        XMVECTOR animRot = XMQuaternionIdentity();

        if (!bone.keyframes.empty())
        {
            if (m_currentTime <= bone.keyframes.front().frame)
            {
                animPos = XMLoadFloat3(&bone.keyframes.front().position);
                animRot = XMQuaternionNormalize(
                    XMLoadFloat4(&bone.keyframes.front().rotation));
            }
            else if (m_currentTime >= bone.keyframes.back().frame)
            {
                animPos = XMLoadFloat3(&bone.keyframes.back().position);
                animRot = XMQuaternionNormalize(
                    XMLoadFloat4(&bone.keyframes.back().rotation));
            }
            else
            {
                for (size_t k = 0; k + 1 < bone.keyframes.size(); ++k)
                {
                    const VMDBoneKeyframe& k0 = bone.keyframes[k];
                    const VMDBoneKeyframe& k1 = bone.keyframes[k + 1];

                    if (m_currentTime >= k0.frame &&
                        m_currentTime <= k1.frame)
                    {
                        float t = 0.0f;
                        if (k1.frame > k0.frame)
                        {
                            t = (m_currentTime - static_cast<float>(k0.frame)) /
                                static_cast<float>(k1.frame - k0.frame);
                        }

                        animPos = XMVectorLerp(
                            XMLoadFloat3(&k0.position),
                            XMLoadFloat3(&k1.position),
                            t);

                        const XMVECTOR r0 = XMQuaternionNormalize(
                            XMLoadFloat4(&k0.rotation));
                        const XMVECTOR r1 = XMQuaternionNormalize(
                            XMLoadFloat4(&k1.rotation));
                        animRot = XMQuaternionNormalize(
                            XMQuaternionSlerp(r0, r1, t));
                        break;
                    }
                }
            }
        }

        const XMVECTOR bindTranslation =
            XMLoadFloat3(&bone.localBindPosition);
        const XMVECTOR localTranslation =
            XMVectorAdd(bindTranslation, animPos);

        localMatrices[i] = XMMatrixAffineTransformation(
            XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f),
            XMVectorZero(),
            animRot,
            localTranslation);
    }

    // Resolve the hierarchy recursively.  The old code only used a parent when
    // parentIndex < childIndex; valid PMX files do not require that ordering.
    std::function<void(size_t)> resolveGlobal = [&](size_t index)
    {
        if (resolveState[index] == 2)
            return;

        // Break malformed cyclic hierarchies safely.
        if (resolveState[index] == 1)
        {
            globalMatrices[index] = localMatrices[index];
            resolveState[index] = 2;
            return;
        }

        resolveState[index] = 1;

        const int parentIndex = m_bones[index].parentIndex;
        if (parentIndex >= 0 &&
            parentIndex < static_cast<int>(boneCount) &&
            parentIndex != static_cast<int>(index))
        {
            resolveGlobal(static_cast<size_t>(parentIndex));
            globalMatrices[index] = XMMatrixMultiply(
                localMatrices[index],
                globalMatrices[parentIndex]);
        }
        else
        {
            globalMatrices[index] = localMatrices[index];
        }

        resolveState[index] = 2;
    };

    for (size_t i = 0; i < boneCount; ++i)
        resolveGlobal(i);

    // Reset unused entries as identity so stale matrices can never affect a
    // later model with fewer bones.
    m_skinningMatrices.assign(1024, XMMatrixIdentity());

    const size_t outputCount = boneCount < 1024 ? boneCount : 1024;
    for (size_t i = 0; i < outputCount; ++i)
    {
        const BoneNode& bone = m_bones[i];
        const XMMATRIX inverseBind = XMMatrixTranslation(
            bone.inverseBindTranslation.x,
            bone.inverseBindTranslation.y,
            bone.inverseBindTranslation.z);

        // GeometryPass uses mul(rowVector, matrix), so upload transposed data.
        m_skinningMatrices[i] = XMMatrixTranspose(
            XMMatrixMultiply(inverseBind, globalMatrices[i]));
    }
}
