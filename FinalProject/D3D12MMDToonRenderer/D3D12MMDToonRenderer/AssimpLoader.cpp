#include "stdafx.h"
#include "AssimpLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <codecvt>
#include <locale>
#include <Windows.h>

bool AssimpLoader::Load(const std::wstring& filename)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::string narrowFilename = converter.to_bytes(filename);

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(narrowFilename,
        aiProcess_Triangulate | aiProcess_FlipUVs |
        aiProcess_GenNormals | aiProcess_ConvertToLeftHanded);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        OutputDebugStringA(importer.GetErrorString());
        MessageBoxW(nullptr, L"Can't found the model!", L"Error", MB_OK);
        return false;
    }

    UINT currentVertexOffset = 0;
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        aiMesh* mesh = scene->mMeshes[m];

        // Get the Path
        std::wstring modelDir = L"";
        size_t dirLastSlash = filename.find_last_of(L"\\/");
        if (dirLastSlash != std::wstring::npos) {
            modelDir = filename.substr(0, dirLastSlash + 1);
        }

        // Read the path of material's textures
        std::wstring texPath = L"MISSING_TEXTURE";
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString path;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS) {
                std::string rawPath = path.C_Str();
                size_t lastDot = rawPath.find_last_of(".");
                if (lastDot != std::string::npos) rawPath = rawPath.substr(0, lastDot) + ".dds";
                size_t lastSlash = rawPath.find_last_of("\\/");
                std::string fileName = (lastSlash == std::string::npos) ? rawPath : rawPath.substr(lastSlash + 1);
                texPath = modelDir + converter.from_bytes(fileName);
            }
        }

        AssimpMesh data = {};
        data.startIndex = currentVertexOffset;
        data.vertexCount = mesh->mNumFaces * 3;
        data.texturePath = texPath;
        m_meshes.push_back(data);

        // Flatten
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            const aiFace& face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                unsigned int idx = face.mIndices[j];
                AssimpVertex vertex = {};
                vertex.position = { mesh->mVertices[idx].x, mesh->mVertices[idx].y, mesh->mVertices[idx].z };
                if (mesh->HasNormals()) {
                    vertex.normal = { mesh->mNormals[idx].x, mesh->mNormals[idx].y, mesh->mNormals[idx].z };
                }
                if (mesh->HasTextureCoords(0)) {
                    vertex.uv = { mesh->mTextureCoords[0][idx].x, mesh->mTextureCoords[0][idx].y };
                }
                m_vertices.push_back(vertex);
            }
        }
        currentVertexOffset += data.vertexCount;
    }
    return true;
}