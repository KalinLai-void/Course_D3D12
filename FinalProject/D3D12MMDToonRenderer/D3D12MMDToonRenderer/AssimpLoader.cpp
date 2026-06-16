#include "stdafx.h"
#include "AssimpLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#include <algorithm>
#include <codecvt>
#include <cwctype>
#include <locale>
#include <Windows.h>

namespace
{
    float Clamp01(float value)
    {
        if (value < 0.0f)
            return 0.0f;

        if (value > 1.0f)
            return 1.0f;

        return value;
    }

    bool FileExists(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::wstring NormalizeSlashes(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'\\', L'/');
        return path;
    }

    std::wstring GetDirectory(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"";
        return path.substr(0, slash + 1);
    }

    std::wstring GetFileName(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return path;
        return path.substr(slash + 1);
    }

    std::wstring ReplaceExtension(
        const std::wstring& path,
        const std::wstring& newExtension)
    {
        const size_t slash = path.find_last_of(L"\\/");
        const size_t dot = path.find_last_of(L'.');

        if (dot == std::wstring::npos ||
            (slash != std::wstring::npos && dot < slash))
        {
            return path + newExtension;
        }

        return path.substr(0, dot) + newExtension;
    }

    std::wstring SafeFromUtf8(
        const aiString& source,
        std::wstring_convert<std::codecvt_utf8<wchar_t>>& converter)
    {
        try
        {
            return converter.from_bytes(source.C_Str());
        }
        catch (...)
        {
            return L"";
        }
    }

    std::wstring GetLowerExtension(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        const size_t dot = path.find_last_of(L'.');

        if (dot == std::wstring::npos ||
            (slash != std::wstring::npos && dot < slash))
        {
            return L"";
        }

        std::wstring extension = path.substr(dot);
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](wchar_t c)
            {
                return static_cast<wchar_t>(towlower(c));
            });

        return extension;
    }

    bool IsDirectlyLoadableTexture(const std::wstring& path)
    {
        const std::wstring extension = GetLowerExtension(path);

        return extension == L".dds" ||
               extension == L".png" ||
               extension == L".jpg" ||
               extension == L".jpeg" ||
               extension == L".bmp" ||
               extension == L".tif" ||
               extension == L".tiff" ||
               extension == L".gif";
    }

    bool FindFileRecursive(
        const std::wstring& directory,
        const std::wstring& targetFileName,
        std::wstring& result)
    {
        std::wstring normalizedDirectory = NormalizeSlashes(directory);

        if (!normalizedDirectory.empty() &&
            normalizedDirectory.back() != L'/')
        {
            normalizedDirectory += L'/';
        }

        WIN32_FIND_DATAW findData = {};
        const std::wstring searchPattern =
            normalizedDirectory + L"*";

        HANDLE handle =
            FindFirstFileW(searchPattern.c_str(), &findData);

        if (handle == INVALID_HANDLE_VALUE)
            return false;

        bool found = false;

        do
        {
            const std::wstring name = findData.cFileName;

            if (name == L"." || name == L"..")
                continue;

            const std::wstring fullPath =
                normalizedDirectory + name;

            if ((findData.dwFileAttributes &
                 FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                // Avoid traversing directory junctions/symlinks.
                if ((findData.dwFileAttributes &
                     FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                {
                    if (FindFileRecursive(
                            fullPath,
                            targetFileName,
                            result))
                    {
                        found = true;
                        break;
                    }
                }
            }
            else if (_wcsicmp(
                         name.c_str(),
                         targetFileName.c_str()) == 0)
            {
                result = NormalizeSlashes(fullPath);
                found = true;
                break;
            }
        }
        while (FindNextFileW(handle, &findData));

        FindClose(handle);
        return found;
    }

    bool UsesDiffuseAlphaCutout(const std::wstring& texturePath)
    {
        std::wstring fileName = GetFileName(texturePath);

        std::transform(
            fileName.begin(),
            fileName.end(),
            fileName.begin(),
            [](wchar_t c)
            {
                return static_cast<wchar_t>(towlower(c));
            });

        // The uploaded Sponza package stores cutout alpha directly in these
        // diffuse textures. Its MTL contains no map_d entries.
        return fileName == L"sponza_thorn.dds" ||
               fileName == L"sponza_thorn.tga" ||
               fileName == L"vase_plant.dds" ||
               fileName == L"vase_plant.tga" ||
               fileName == L"chain_texture.dds" ||
               fileName == L"chain_texture.tga";
    }

    // C++14 version. No std::filesystem is used.
    std::wstring ResolveTexturePath(
        const std::wstring& modelFilename,
        const aiString& importedPath,
        std::wstring_convert<std::codecvt_utf8<wchar_t>>& converter)
    {
        std::wstring rawPath = SafeFromUtf8(importedPath, converter);
        if (rawPath.empty())
            return L"";

        rawPath = NormalizeSlashes(rawPath);

        const std::wstring modelDirectory =
            NormalizeSlashes(GetDirectory(modelFilename));
        const std::wstring fileName = GetFileName(rawPath);

        const std::wstring candidates[] =
        {
            rawPath,
            modelDirectory + rawPath,
            modelDirectory + fileName
        };

        for (size_t i = 0; i < _countof(candidates); ++i)
        {
            const std::wstring candidate =
                NormalizeSlashes(candidates[i]);

            // Prefer a DDS with the same path and base filename.
            const std::wstring ddsCandidate =
                ReplaceExtension(candidate, L".dds");

            if (FileExists(ddsCandidate))
                return ddsCandidate;

            // WICTextureLoader does not decode TGA. Only return formats that
            // the current renderer can actually load.
            if (FileExists(candidate) &&
                IsDirectlyLoadableTexture(candidate))
            {
                return candidate;
            }
        }

        // Some Sponza packages place converted DDS files in a different
        // subfolder. Search below the OBJ directory by filename.
        const std::wstring ddsFileName =
            GetFileName(ReplaceExtension(rawPath, L".dds"));

        std::wstring recursiveResult;

        if (FindFileRecursive(
                modelDirectory,
                ddsFileName,
                recursiveResult))
        {
            return recursiveResult;
        }

        // Search the original filename only when it is directly loadable.
        if (IsDirectlyLoadableTexture(fileName) &&
            FindFileRecursive(
                modelDirectory,
                fileName,
                recursiveResult))
        {
            return recursiveResult;
        }

        // A TGA may exist, but the current WIC-based loader cannot decode it.
        if (GetLowerExtension(rawPath) == L".tga")
        {
            OutputDebugStringW(
                (L"[Assimp][TGA unsupported] Convert this texture to DDS: " +
                 rawPath +
                 L"\n").c_str());
        }

        return L"";
    }
}

bool AssimpLoader::Load(const std::wstring& filename)
{
    m_vertices.clear();
    m_meshes.clear();

    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    const std::string narrowFilename = converter.to_bytes(filename);

    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(
        narrowFilename,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_MakeLeftHanded |
        aiProcess_FlipWindingOrder);

    if (!scene ||
        (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 ||
        scene->mRootNode == nullptr)
    {
        OutputDebugStringA(importer.GetErrorString());
        MessageBoxW(nullptr, L"Can't load the model.", L"Error", MB_OK);
        return false;
    }

    UINT currentVertexOffset = 0;

    for (unsigned int meshIndex = 0;
         meshIndex < scene->mNumMeshes;
         ++meshIndex)
    {
        aiMesh* mesh = scene->mMeshes[meshIndex];

        AssimpMesh data = {};
        data.startIndex = currentVertexOffset;
        data.vertexCount = mesh->mNumFaces * 3;
        data.materialName = L"(unnamed material)";
        data.hasDiffuseTexture = false;
        data.diffuseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        data.diffuseTexturePath = L"";
        data.opacityMode = ASSIMP_OPACITY_OPAQUE;

        if (mesh->mMaterialIndex < scene->mNumMaterials)
        {
            aiMaterial* material =
                scene->mMaterials[mesh->mMaterialIndex];

            aiString importedMaterialName;
            if (material->Get(AI_MATKEY_NAME, importedMaterialName) == AI_SUCCESS)
            {
                const std::wstring convertedName =
                    SafeFromUtf8(importedMaterialName, converter);
                if (!convertedName.empty())
                    data.materialName = convertedName;
            }

            aiColor3D kd(1.0f, 1.0f, 1.0f);
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, kd) == AI_SUCCESS)
            {
                data.diffuseColor.x = kd.r;
                data.diffuseColor.y = kd.g;
                data.diffuseColor.z = kd.b;
            }

            // map_Kd
            aiString importedDiffusePath;
            if (material->GetTexture(
                    aiTextureType_DIFFUSE,
                    0,
                    &importedDiffusePath) == AI_SUCCESS)
            {
                const std::wstring resolvedPath =
                    ResolveTexturePath(
                        filename,
                        importedDiffusePath,
                        converter);

                if (!resolvedPath.empty())
                {
                    data.hasDiffuseTexture = true;
                    data.diffuseTexturePath = resolvedPath;
                }
                else
                {
                    // The material declared map_Kd but the file is unavailable.
                    // Keep this distinct from a valid textureless Kd material.
                    data.hasDiffuseTexture = true;
                    data.diffuseTexturePath = L"MISSING_TEXTURE";

                    OutputDebugStringW(
                        (L"[Assimp][Missing map_Kd] material=" +
                         data.materialName +
                         L", source=" +
                         SafeFromUtf8(importedDiffusePath, converter) +
                         L"\n").c_str());
                }
            }

            // The uploaded sponza.mtl has no map_d entries. Alpha cutout is
            // embedded only in sponza_thorn, vase_plant and chain_texture.
            if (data.hasDiffuseTexture &&
                data.diffuseTexturePath != L"MISSING_TEXTURE" &&
                UsesDiffuseAlphaCutout(data.diffuseTexturePath))
            {
                data.opacityMode = ASSIMP_OPACITY_DIFFUSE_ALPHA;
            }
            else
            {
                data.opacityMode = ASSIMP_OPACITY_OPAQUE;
            }
        }

        m_meshes.push_back(data);

        wchar_t materialDebug[1024] = {};
        swprintf_s(
            materialDebug,
            L"[Sponza Material] name=%s, Kd=(%.3f, %.3f, %.3f), diffuse=%s, alphaMode=%s\n",
            data.materialName.c_str(),
            data.diffuseColor.x,
            data.diffuseColor.y,
            data.diffuseColor.z,
            data.hasDiffuseTexture ? data.diffuseTexturePath.c_str() : L"<Kd solid color>",
            data.opacityMode == ASSIMP_OPACITY_DIFFUSE_ALPHA
                ? L"DIFFUSE_ALPHA"
                : L"OPAQUE");
        OutputDebugStringW(materialDebug);

        // Flatten indexed Assimp geometry so DrawInstanced can use
        // StartVertexLocation directly.
        for (unsigned int faceIndex = 0;
             faceIndex < mesh->mNumFaces;
             ++faceIndex)
        {
            const aiFace& face = mesh->mFaces[faceIndex];

            for (unsigned int indexInFace = 0;
                 indexInFace < face.mNumIndices;
                 ++indexInFace)
            {
                const unsigned int vertexIndex =
                    face.mIndices[indexInFace];

                AssimpVertex vertex = {};

                vertex.position =
                {
                    mesh->mVertices[vertexIndex].x,
                    mesh->mVertices[vertexIndex].y,
                    mesh->mVertices[vertexIndex].z
                };

                if (mesh->HasNormals())
                {
                    vertex.normal =
                    {
                        mesh->mNormals[vertexIndex].x,
                        mesh->mNormals[vertexIndex].y,
                        mesh->mNormals[vertexIndex].z
                    };
                }

                if (mesh->HasTextureCoords(0))
                {
                    vertex.uv =
                    {
                        mesh->mTextureCoords[0][vertexIndex].x,
                        mesh->mTextureCoords[0][vertexIndex].y
                    };
                }

                m_vertices.push_back(vertex);
            }
        }

        currentVertexOffset += data.vertexCount;
    }

    return true;
}
