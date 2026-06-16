#include "stdafx.h"
#include "PMXLoader.h"
#include <fstream>
#include <codecvt>
#include <locale>
#include <Windows.h>
#include <vector>

namespace {
    inline int ReadIndex(std::ifstream& file, int size) {
        int index = 0;
        if (size == 1) { uint8_t v; file.read((char*)&v, 1); index = (v == 0xFF) ? -1 : v; }
        else if (size == 2) { uint16_t v; file.read((char*)&v, 2); index = (v == 0xFFFF) ? -1 : v; }
        else if (size == 4) { file.read((char*)&index, 4); }
        return index;
    }

    inline std::wstring UTF8ToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }

    inline std::wstring ReadPMXString(std::ifstream& file, bool isUTF8) {
        int length;
        file.read((char*)&length, 4);
        if (length <= 0) return L"";
        std::vector<char> buffer(length);
        file.read(buffer.data(), length);
        if (isUTF8) return UTF8ToWString(std::string(buffer.begin(), buffer.end()));
        else return std::wstring((wchar_t*)buffer.data(), length / 2);
    }
}

bool PMXLoader::Load(const std::wstring& filename)
{
    std::string narrowFilename(filename.begin(), filename.end());
    std::ifstream file(narrowFilename, std::ios::binary);
    if (!file) {
        MessageBoxA(nullptr, "Failed to open PMX file!", "Error", MB_OK);
        return false;
    }

    // 1. Header
    char magic[4]; file.read(magic, 4);
    if (magic[0] != 'P' || magic[1] != 'M' || magic[2] != 'X') return false;

    float version; file.read((char*)&version, 4);
    uint8_t globalCount; file.read((char*)&globalCount, 1);
    std::vector<uint8_t> globals(globalCount);
    file.read((char*)globals.data(), globalCount);

    bool isUTF8 = (globals[0] == 1);
    int addUVCount = globals[1];
    int vertexIndexSize = globals[2];
    int textureIndexSize = globals[3];
    int materialIndexSize = globals[4];
    int boneIndexSize = globals[5];

    ReadPMXString(file, isUTF8); ReadPMXString(file, isUTF8);
    ReadPMXString(file, isUTF8); ReadPMXString(file, isUTF8);

    // 2. Vertices
    int vertexCount; file.read((char*)&vertexCount, 4);
    m_vertices.clear(); m_vertices.reserve(vertexCount);
    for (int i = 0; i < vertexCount; ++i) {
        PMXVertex v = {};
        file.read((char*)&v.position, 12);
        file.read((char*)&v.normal, 12);
        file.read((char*)&v.uv, 8);
        v.position.z = -v.position.z;
        v.normal.z = -v.normal.z;

        file.seekg(addUVCount * 16, std::ios::cur);
        uint8_t weightType; file.read((char*)&weightType, 1);

        auto readBoneIndex = [&]() -> uint32_t {
            int idx = ReadIndex(file, boneIndexSize);
            return (idx >= 0) ? (uint32_t)idx : 0;
            };

        v.boneIndices[0] = v.boneIndices[1] = v.boneIndices[2] = v.boneIndices[3] = 0;
        v.boneWeights[0] = v.boneWeights[1] = v.boneWeights[2] = v.boneWeights[3] = 0.0f;

        switch (weightType) {
        case 0: v.boneIndices[0] = readBoneIndex(); v.boneWeights[0] = 1.0f; break;
        case 1:
            v.boneIndices[0] = readBoneIndex(); v.boneIndices[1] = readBoneIndex();
            file.read((char*)&v.boneWeights[0], 4);
            v.boneWeights[1] = 1.0f - v.boneWeights[0];
            break;
        case 2: case 4:
            v.boneIndices[0] = readBoneIndex(); v.boneIndices[1] = readBoneIndex();
            v.boneIndices[2] = readBoneIndex(); v.boneIndices[3] = readBoneIndex();
            file.read((char*)&v.boneWeights[0], 4); file.read((char*)&v.boneWeights[1], 4);
            file.read((char*)&v.boneWeights[2], 4); file.read((char*)&v.boneWeights[3], 4);
            break;
        case 3:
            v.boneIndices[0] = readBoneIndex(); v.boneIndices[1] = readBoneIndex();
            file.read((char*)&v.boneWeights[0], 4);
            v.boneWeights[1] = 1.0f - v.boneWeights[0];
            file.seekg(36, std::ios::cur);
            break;
        }
        file.seekg(4, std::ios::cur);
        m_vertices.push_back(v);
    }

    // ==========================================
    // 3. Indices (之前被誤刪的關鍵區塊)
    // ==========================================
    int indexCount; file.read((char*)&indexCount, 4);
    m_indices.clear(); m_indices.reserve(indexCount);
    for (int i = 0; i < indexCount; i += 3) {
        uint32_t i0 = ReadIndex(file, vertexIndexSize);
        uint32_t i1 = ReadIndex(file, vertexIndexSize);
        uint32_t i2 = ReadIndex(file, vertexIndexSize);
        m_indices.push_back(i0);
        m_indices.push_back(i2);
        m_indices.push_back(i1);
    }

    // ==========================================
    // 4. Textures (之前被誤刪的關鍵區塊)
    // ==========================================
    int textureCount; file.read((char*)&textureCount, 4);
    std::vector<std::wstring> texturePaths;
    for (int i = 0; i < textureCount; ++i) {
        texturePaths.push_back(ReadPMXString(file, isUTF8));
    }

    // ==========================================
    // 5. Materials (之前被誤刪的關鍵區塊)
    // ==========================================
    int materialCount; file.read((char*)&materialCount, 4);
    m_materials.clear();

    std::wstring modelDir = L"";
    size_t dirLastSlash = filename.find_last_of(L"\\/");
    if (dirLastSlash != std::wstring::npos) {
        modelDir = filename.substr(0, dirLastSlash + 1);
    }

    for (int i = 0; i < materialCount; ++i) {
        ReadPMXString(file, isUTF8);
        ReadPMXString(file, isUTF8);

        file.seekg(16 + 12 + 4 + 12 + 1 + 16 + 4, std::ios::cur);

        int texIndex = ReadIndex(file, textureIndexSize);
        file.seekg(textureIndexSize + 1, std::ios::cur);

        uint8_t toonFlag; file.read((char*)&toonFlag, 1);
        if (toonFlag == 0) ReadIndex(file, textureIndexSize);
        else file.seekg(1, std::ios::cur);

        ReadPMXString(file, isUTF8);

        int faceIndexCount; file.read((char*)&faceIndexCount, 4);

        PMXMaterial mat = {};
        mat.indexCount = faceIndexCount;
        if (texIndex >= 0 && texIndex < textureCount) {
            std::wstring texPath = modelDir + texturePaths[texIndex];
            for (wchar_t& c : texPath) if (c == L'\\') c = L'/';
            mat.texturePath = texPath;
        }
        else {
            mat.texturePath = L"MISSING_TEXTURE";
        }
        m_materials.push_back(mat);
    }

    // ==========================================
    // 6. Bones
    // ==========================================
    int boneCount; file.read((char*)&boneCount, 4);
    m_bones.clear(); m_bones.reserve(boneCount);

    for (int i = 0; i < boneCount; ++i) {
        PMXBone bone = {};
        bone.name = ReadPMXString(file, isUTF8);
        ReadPMXString(file, isUTF8);

        file.read((char*)&bone.position, 12);
        bone.position.z = -bone.position.z;

        bone.parentIndex = ReadIndex(file, boneIndexSize);
        file.seekg(4, std::ios::cur);

        uint16_t boneFlags; file.read((char*)&boneFlags, 2);

        if (boneFlags & 0x0001) ReadIndex(file, boneIndexSize);
        else file.seekg(12, std::ios::cur);

        if (boneFlags & (0x0100 | 0x0200)) {
            ReadIndex(file, boneIndexSize);
            file.seekg(4, std::ios::cur);
        }

        if (boneFlags & 0x0400) file.seekg(12, std::ios::cur);
        if (boneFlags & 0x0800) file.seekg(24, std::ios::cur);
        if (boneFlags & 0x2000) file.seekg(4, std::ios::cur);

        if (boneFlags & 0x0020) {
            ReadIndex(file, boneIndexSize);
            file.seekg(8, std::ios::cur);
            int ikLinkCount; file.read((char*)&ikLinkCount, 4);
            for (int l = 0; l < ikLinkCount; ++l) {
                ReadIndex(file, boneIndexSize);
                uint8_t hasLimit; file.read((char*)&hasLimit, 1);
                if (hasLimit == 1) file.seekg(24, std::ios::cur);
            }
        }
        m_bones.push_back(bone);
    }
    return true;
}