#include "stdafx.h"
#include "PMXLoader.h"
#include <fstream>
#include <codecvt>
#include <locale>
#include <Windows.h>
#include <vector>

namespace {
    // 輔助函式：讀取 PMX 的可變長度 Index
    inline int ReadIndex(std::ifstream& file, int size) {
        int index = 0;
        if (size == 1) { uint8_t v; file.read((char*)&v, 1); index = (v == 0xFF) ? -1 : v; }
        else if (size == 2) { uint16_t v; file.read((char*)&v, 2); index = (v == 0xFFFF) ? -1 : v; }
        else if (size == 4) { file.read((char*)&index, 4); }
        return index;
    }

    // 輔助函式：UTF-8 轉 wstring
    inline std::wstring UTF8ToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }

    // 輔助函式：讀取 PMX 字串 (修正版)
    inline std::wstring ReadPMXString(std::ifstream& file, bool isUTF8) {
        int length;
        file.read((char*)&length, 4);

        if (length <= 0) return L"";

        std::vector<char> buffer(length);
        file.read(buffer.data(), length);

        if (isUTF8) {
            return UTF8ToWString(std::string(buffer.begin(), buffer.end()));
        }
        else {
            // UTF-16LE 轉換：PMX 格式字元為 2 bytes，直接強轉即可
            return std::wstring((wchar_t*)buffer.data(), length / 2);
        }
    }
}

bool PMXLoader::Load(const std::wstring& filename)
{
    // 將 filename 轉為用於 ifstream 的窄字串
    // 注意：若路徑包含非 ASCII 字元，建議使用 C++17 <filesystem> 讀取
    std::string narrowFilename(filename.begin(), filename.end());

    std::ifstream file(narrowFilename, std::ios::binary);
    if (!file) {
        MessageBoxA(nullptr, "Failed to open PMX file!", "Error", MB_OK);
        return false;
    }

    // 1. 讀取檔頭 (Header)
    char magic[4]; file.read(magic, 4);
    if (magic[0] != 'P' || magic[1] != 'M' || magic[2] != 'X' || magic[3] != ' ') {
        MessageBoxA(nullptr, "Not a valid PMX file!", "Error", MB_OK);
        return false;
    }

    float version; file.read((char*)&version, 4);
    uint8_t globalCount; file.read((char*)&globalCount, 1);

    std::vector<uint8_t> globals(globalCount);
    file.read((char*)globals.data(), globalCount);

    // 關鍵：判斷編碼 (globals[0] == 1 表示 UTF-8，0 表示 UTF-16LE)
    bool isUTF8 = (globals[0] == 1);

    int addUVCount = globals[1];
    int vertexIndexSize = globals[2];
    int textureIndexSize = globals[3];
    int materialIndexSize = globals[4];
    int boneIndexSize = globals[5];

    // 跳過模型資訊
    ReadPMXString(file, isUTF8); // Name JP
    ReadPMXString(file, isUTF8); // Name EN
    ReadPMXString(file, isUTF8); // Comment JP
    ReadPMXString(file, isUTF8); // Comment EN

    // 2. 讀取頂點 (Vertices)
    int vertexCount; file.read((char*)&vertexCount, 4);
    m_vertices.clear();
    m_vertices.reserve(vertexCount);

    for (int i = 0; i < vertexCount; ++i) {
        PMXVertex v = {};
        file.read((char*)&v.position, 12);
        file.read((char*)&v.normal, 12);
        file.read((char*)&v.uv, 8);

        v.position.z = -v.position.z;
        v.normal.z = -v.normal.z;
        v.boneWeights[0] = 0.0f;

        file.seekg(addUVCount * 16, std::ios::cur);

        uint8_t weightType; file.read((char*)&weightType, 1);
        switch (weightType) {
        case 0: file.seekg(boneIndexSize, std::ios::cur); break;
        case 1: file.seekg(boneIndexSize * 2 + 4, std::ios::cur); break;
        case 2:
        case 4: file.seekg(boneIndexSize * 4 + 16, std::ios::cur); break;
        case 3: file.seekg(boneIndexSize * 2 + 4 + 36, std::ios::cur); break;
        }
        file.seekg(4, std::ios::cur);
        m_vertices.push_back(v);
    }

    // 3. 讀取索引 (Indices)
    int indexCount; file.read((char*)&indexCount, 4);
    m_indices.clear();
    m_indices.reserve(indexCount);

    for (int i = 0; i < indexCount; i += 3) {
        uint32_t i0 = ReadIndex(file, vertexIndexSize);
        uint32_t i1 = ReadIndex(file, vertexIndexSize);
        uint32_t i2 = ReadIndex(file, vertexIndexSize);
        m_indices.push_back(i0);
        m_indices.push_back(i2);
        m_indices.push_back(i1);
    }

    // 4. 讀取貼圖清單 (Textures)
    int textureCount; file.read((char*)&textureCount, 4);
    std::vector<std::wstring> texturePaths; // 這裡改成 wstring
    for (int i = 0; i < textureCount; ++i) {
        texturePaths.push_back(ReadPMXString(file, isUTF8));
    }

    // 5. 讀取材質 (Materials)
    int materialCount; file.read((char*)&materialCount, 4);
    m_materials.clear();

    // 取得模型所在目錄
    std::wstring modelDir = L"";
    size_t dirLastSlash = filename.find_last_of(L"\\/");
    if (dirLastSlash != std::wstring::npos) {
        modelDir = filename.substr(0, dirLastSlash + 1);
    }

    for (int i = 0; i < materialCount; ++i) {
        ReadPMXString(file, isUTF8); // Name JP
        ReadPMXString(file, isUTF8); // Name EN

        file.seekg(16 + 12 + 4 + 12 + 1 + 16 + 4, std::ios::cur);

        int texIndex = ReadIndex(file, textureIndexSize);
        int envTexIndex = ReadIndex(file, textureIndexSize);
        file.seekg(1, std::ios::cur);

        uint8_t toonFlag;
        file.read((char*)&toonFlag, 1);
        if (toonFlag == 0) file.seekg(textureIndexSize, std::ios::cur);
        else file.seekg(1, std::ios::cur);

        ReadPMXString(file, isUTF8); // Memo
        int faceIndexCount;
        file.read((char*)&faceIndexCount, 4);

        PMXMaterial mat = {};
        mat.indexCount = faceIndexCount;

        if (texIndex >= 0 && texIndex < (int)texturePaths.size()) {
            // 直接拼接 wstring
            std::wstring texPath = modelDir + texturePaths[texIndex];
            for (wchar_t& c : texPath) { if (c == L'\\') c = L'/'; }
            mat.texturePath = texPath;
        }
        else {
            mat.texturePath = L"MISSING_TEXTURE";
        }

        m_materials.push_back(mat);
    }

    return true;
}