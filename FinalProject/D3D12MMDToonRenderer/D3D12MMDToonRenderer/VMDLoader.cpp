#include "stdafx.h"
#include "VMDLoader.h"
#include <fstream>
#include <algorithm>
#include <Windows.h>

namespace {
    // 🛡️ 核心解法：強制將 Shift-JIS (CP 932) 轉為 wstring
    inline std::wstring ShiftJISToWString(const char* buffer, size_t size) {
        // 找到字串的結尾 (VMD 的字串是用 0x00 填滿剩下的空間)
        size_t len = 0;
        while (len < size && buffer[len] != '\0') len++;
        if (len == 0) return L"";

        // Code Page 932 是微軟的 Shift-JIS 標準
        int size_needed = MultiByteToWideChar(932, 0, buffer, (int)len, NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(932, 0, buffer, (int)len, &wstr[0], size_needed);
        return wstr;
    }
}

bool VMDLoader::Load(const std::wstring& filename)
{
    // 將 wstring 路徑轉回 string 以供 ifstream 使用
    std::string narrowFilename(filename.begin(), filename.end());
    std::ifstream file(narrowFilename, std::ios::binary);

    if (!file) {
        MessageBoxA(nullptr, "Failed to open VMD file!", "Error", MB_OK);
        return false;
    }

    // 1. 讀取 VMD 檔頭 (Header) - 30 bytes
    char header[30];
    file.read(header, 30);
    std::string headerStr(header, 25);
    if (headerStr != "Vocaloid Motion Data 0002") {
        MessageBoxA(nullptr, "Not a valid VMD file (Version mismatch)!", "Error", MB_OK);
        return false;
    }

    // 2. 讀取模型名稱 - 20 bytes (我們不需要，跳過)
    file.seekg(20, std::ios::cur);

    // 3. 讀取骨骼影格數量
    uint32_t boneKeyframeCount = 0;
    file.read((char*)&boneKeyframeCount, 4);

    // 暫存陣列
    std::vector<VMDBoneKeyframe> flatKeyframes;
    flatKeyframes.reserve(boneKeyframeCount);
    m_maxFrame = 0;

    // 4. 解析所有骨骼關鍵影格
    for (uint32_t i = 0; i < boneKeyframeCount; ++i) {
        VMDBoneKeyframe keyframe = {};

        // 讀取並轉換骨骼名稱 (15 bytes, Shift-JIS)
        char boneNameBuf[15];
        file.read(boneNameBuf, 15);
        keyframe.boneName = ShiftJISToWString(boneNameBuf, 15);

        // 讀取幀數
        file.read((char*)&keyframe.frame, 4);
        if (keyframe.frame > m_maxFrame) {
            m_maxFrame = keyframe.frame;
        }

        // 讀取位置偏移量 (12 bytes)
        file.read((char*)&keyframe.position, 12);
        // 🚨 右手座標系轉左手座標系 (Z 軸反轉)
        keyframe.position.z = -keyframe.position.z;

        // 讀取旋轉四元數 (16 bytes)
        file.read((char*)&keyframe.rotation, 16);
        // 🚨 四元數的左右手轉換 (將 X 與 Y 軸反轉)
        keyframe.rotation.x = -keyframe.rotation.x;
        keyframe.rotation.y = -keyframe.rotation.y;

        // 跳過貝茲曲線插值資料 (64 bytes)
        // 為了極限速通，我們等一下直接用線性插值 (Slerp/Lerp)，放棄複雜的貝茲曲線
        file.seekg(64, std::ios::cur);

        flatKeyframes.push_back(keyframe);
    }

    // 5. 整理資料：VMD 的影格是亂序的，我們必須按骨頭名稱分組，並依據幀數排序
    m_boneAnimations.clear();
    for (const auto& kf : flatKeyframes) {
        m_boneAnimations[kf.boneName].push_back(kf);
    }

    // 將每個骨頭內部的影格依照 frame 數字從小到大排序
    for (auto& pair : m_boneAnimations) {
        std::sort(pair.second.begin(), pair.second.end(),
            [](const VMDBoneKeyframe& a, const VMDBoneKeyframe& b) {
                return a.frame < b.frame;
            });
    }

    return true;
}