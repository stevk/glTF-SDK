// glTF-Interpolator.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include "pch.h"
#include "StreamReader.h"
#include "StreamWriter.h"
#include <GLTFSDK/Deserialize.h>
#include <GLTFSDK/Serialize.h>
#include <GLTFSDK/GLTFResourceWriter.h>
#include <GLTFSDK/GLTFResourceReader.h>
#include <GLTFSDK/BufferBuilder.h>
#include <GLTFSDK/IStreamWriter.h>
#include <GLTFSDK/GLTF.h>
#include <Eigen/Dense>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cassert>
#include <cstdlib>

using namespace std;
using namespace Microsoft::glTF;
using namespace Eigen;

int main()
{
    std::experimental::filesystem::path sourcePath = "D:/Code/glTF-SDK/glTF-Interpolator/Data";
    std::experimental::filesystem::path filename = "buggy.gltf";//"Node_Attribute_08.gltf";
    std::experimental::filesystem::path TargetFilename = "MergedMesh.gltf";

    // Load the glTF model
    auto streamReader = make_unique<StreamReader>(sourcePath);
    auto gltfStream = streamReader->GetInputStream(filename.u8string());
    auto gltfResourceReader = make_unique<GLTFResourceReader>(move(streamReader));

    stringstream manifestStream;
    manifestStream << gltfStream->rdbuf();
    string manifestSource = manifestStream.str();

    Document documentSource;
    try
    {
        documentSource = Deserialize(manifestSource);
    }
    catch (const GLTFException& ex)
    {
        stringstream ss;
        ss << "Microsoft::glTF::Deserialize failed: ";
        ss << ex.what();
        throw runtime_error(ss.str());
    }

    cout << "glTF loaded!\n";

    auto streamWriter = make_unique<StreamWriter>(sourcePath);
    auto resourceWriter = make_unique<GLTFResourceWriter>(move(streamWriter));

    Document documentTarget;
    BufferBuilder bufferBuilder(move(resourceWriter));

    const char* bufferId = "MergedMesh";
    bufferBuilder.AddBuffer(bufferId);

    // Apply the transforms from each node to the positions of its mesh and child nodes.
        // The string is the node ID, while the matrix is the transformation matrix.
    vector<pair<string, Matrix4f>> modifiedMatrices;
    for (const auto& nodeMain : documentSource.nodes.Elements())
    {
        Matrix4f matrix;
        int index = 0;
        for (auto it = nodeMain.matrix.values.begin(); it != nodeMain.matrix.values.end(); it++)
        {
            int row = index / 4;
            int col = index % 4;
            matrix((row), (col)) = *it;
            index++;
        }

        modifiedMatrices.push_back(make_pair(nodeMain.id, matrix));
    }

    // Flatten the matrix transforms, so every node's matrix also includes the transforms of its parents.
    int index = 0;
    for (const auto& node : documentSource.nodes.Elements())
    {
        for (auto it = node.children.begin(); it != node.children.end(); it++)
        {
            auto searchValue = *it;
            auto childMatrix = find_if(modifiedMatrices.begin(), modifiedMatrices.end(), [&searchValue](const pair<string, Matrix4f>& obj) { return obj.first == searchValue; });
            if (childMatrix->second.any())
            {
                childMatrix->second = modifiedMatrices[index].second * childMatrix->second;
            }
            else
            {
                childMatrix->second = modifiedMatrices[index].second;
            }
        }
        index++;
    }

    // Rebuild the meshes as a single mesh.
    Mesh meshTarget;
    for (const auto& node : documentSource.nodes.Elements())
    {
        // Note that this means a mesh can be called more than once, if it is instanced by more than one node.
        if (node.meshId != "")
        {
            auto mesh = documentSource.meshes.Get(node.meshId);
            for (const auto& meshPrimitive : mesh.primitives)
            {
                // Positions
                string accessorIdPositons;
                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_POSITION, accessorIdPositons))
                {
                    // Load positions.
                    const Accessor& accessorPosition = documentSource.accessors.Get(accessorIdPositons);
                    const auto dataPosition = gltfResourceReader->ReadBinaryData<float>(documentSource, accessorPosition);
                    vector<float> dataNewPosition;
                    // Transform positions.
                    for (auto it = dataPosition.begin(); it != dataPosition.end(); it++)
                    {
                        auto x = *it;
                        it++;
                        auto y = *it;
                        it++;
                        auto z = *it;
                        Vector4f vec(x, y, z, 1);

                        //Vector4f vec(*it, *(it + 1), *(it + 2), 1);
                        auto &nodeId = node.id;
                        auto matrix = find_if(modifiedMatrices.begin(), modifiedMatrices.end(), [nodeId](const pair<string, Matrix4f>& obj) { return obj.first == nodeId; })->second;
                        vec = vec.transpose() * matrix;
                        dataNewPosition.push_back(vec.x());
                        dataNewPosition.push_back(vec.y());
                        dataNewPosition.push_back(vec.z());
                    }

                    // Save positions to buffer, bufferview, and accessor
                    bufferBuilder.AddBufferView(BufferViewTarget::ARRAY_BUFFER);
                    vector<float> minValues(3U, numeric_limits<float>::max());
                    vector<float> maxValues(3U, numeric_limits<float>::lowest());
                    const size_t positionCount = dataNewPosition.size();
                    for (size_t i = 0U, j = 0U; i < positionCount; ++i, j = (i % 3U))
                    {
                        minValues[j] = min(dataNewPosition[i], minValues[j]);
                        maxValues[j] = max(dataNewPosition[i], maxValues[j]);
                    }
                    accessorIdPositons = bufferBuilder.AddAccessor(dataNewPosition, { accessorPosition.type, accessorPosition.componentType, false, move(minValues), move(maxValues) }).id;
                }

                // Indices
                const Accessor& accessorIndices = documentSource.accessors.Get(meshPrimitive.indicesAccessorId);
                const auto dataIndices = gltfResourceReader->ReadBinaryData<uint16_t>(documentSource, accessorIndices);
                bufferBuilder.AddBufferView(BufferViewTarget::ELEMENT_ARRAY_BUFFER);
                string accessorIdIndices = bufferBuilder.AddAccessor(dataIndices, { accessorIndices.type, accessorIndices.componentType }).id;

                // Mesh Primitive
                MeshPrimitive meshPrimitive;
                meshPrimitive.indicesAccessorId = accessorIdIndices;
                meshPrimitive.attributes[ACCESSOR_POSITION] = accessorIdPositons;
                meshTarget.primitives.push_back(move(meshPrimitive));
            }
        }
    }

    // Buffer
    bufferBuilder.Output(documentTarget);

    // Mesh
    auto meshId = documentTarget.meshes.Append(move(meshTarget), AppendIdPolicy::GenerateOnEmpty).id;

    // Node
    Node node;
    node.meshId = meshId;
    auto nodeId = documentTarget.nodes.Append(move(node), AppendIdPolicy::GenerateOnEmpty).id;
    
    // Scene
    Scene scene;
    scene.nodes.push_back(nodeId);
    documentTarget.SetDefaultScene(move(scene), AppendIdPolicy::GenerateOnEmpty);

    cout << "glTF rebuilt!\n";

    // Write the translated model to file.
    string manifestTarget;
    try
    {
        manifestTarget = Serialize(documentTarget, SerializeFlags::Pretty);
    }
    catch (const GLTFException& ex)
    {
        stringstream ss;
        ss << "Microsoft::glTF::Serialize failed: ";
        ss << ex.what();
        throw std::runtime_error(ss.str());
    }

    auto& gltfResourceWriter = bufferBuilder.GetResourceWriter();
    gltfResourceWriter.WriteExternal(TargetFilename.u8string(), manifestTarget);

    cout << "glTF written to new file!\n";

    // Bake each node's (and its parent's) transforms into its mesh primitive's vertex positions.

    // Remove material and Texture Coords from all primitives.

    // Add each primitive to a new mesh in a new node.

    // Discard all textures, images, and texture samplers. (no materials)
    // Discard all skins. (Targets a node and won't work the same with a combined mesh. Loss of potential transforms?)
    // Discard all nodes that don't have a camera attached. (transforms baked into positions. Morph won't work the same with only one mesh)
    // Discard all animation channels that target non-camera nodes. (animations won't work the same with only one mesh.)
    // Discard all animations without channels. (previously discarded channels will reveal which animations are no longer used.)
    // (mesh weights are dropped automatically by using a new base mesh)

    // Write the model to a new file.
}

//vector<pair<string, Matrix4f>> MergeNodeTransforms(IndexedContainer<const Node> &nodes)
//{
//    // Recreates the matrices in a mutable format that will be modified.
//    // The string is the node ID, while the matrix is the transformation matrix.
//    vector<pair<string, Matrix4f>> modifiedMatrices;
//    for (const auto& nodeMain : nodes.Elements())
//    {
//        Matrix4f matrix;
//        int index = 0;
//        for (auto it = nodeMain.matrix.values.begin(); it != nodeMain.matrix.values.end(); it++)
//        {
//            int row = index / 4;
//            int col = index % 4;
//            matrix((row),(col)) = *it;
//            index++;
//        }
//
//        modifiedMatrices.push_back(make_pair(nodeMain.id, matrix));
//    }
//
//    // Flatten the matrix transforms, so every node's matrix also includes the transforms of its parents.
//    int index = 0;
//    for (const auto& node : nodes.Elements())
//    {
//        if (!node.children.empty && !node.matrix.values.empty)
//        {
//            for (auto it = node.children.begin(); it != node.children.end(); it++)
//            {
//                auto searchValue = *it;
//                auto childMatrix = find_if(modifiedMatrices.begin(), modifiedMatrices.end(), [&searchValue](const pair<string, Matrix4f>& obj) { return obj.first == searchValue; } );
//                if (childMatrix->second.any())
//                {
//                    childMatrix->second = modifiedMatrices[index].second * childMatrix->second;
//                }
//                else
//                {
//                    childMatrix->second = modifiedMatrices[index].second;
//                }
//            }
//        }
//        index++;
//    }
//
//    return modifiedMatrices;
//}
//
//Mesh SavePositionsWithTransforms(Document &document, vector<pair<string, Matrix4f>> &modifiedMatrices, BufferBuilder &bufferBuilder, unique_ptr<GLTFResourceReader> &gltfResourceReader)
//{
//    Mesh meshFinal;
//    for (const auto& node : document.nodes.Elements())
//    {
//        if (!node.meshId.empty)
//        {
//            // Note that this means a mesh can be called more than once, if it is instanced by more than one node.
//            auto mesh = document.meshes.Get(node.meshId);
//            for (const auto& meshPrimitive : mesh.primitives)
//            {
//                // Positions
//                string accessorIdPositons;
//                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_POSITION, accessorIdPositons))
//                {
//                    // Load positions.
//                    const Accessor& accessorPosition = document.accessors.Get(accessorIdPositons);
//                    const auto dataPosition = gltfResourceReader->ReadBinaryData<float>(document, accessorPosition);
//                    vector<float> dataNewPosition;
//                    // Transform positions.
//                    for (auto it = dataPosition.begin(); it != dataPosition.end(); it + 3)
//                    {
//                        Vector3f vec(*it, *(it + 1), *(it + 2));
//                        auto &nodeId = node.id;
//                        auto matrix = &find_if(modifiedMatrices.begin(), modifiedMatrices.end(), [nodeId](const pair<string, Matrix4f>& obj) { return obj.first == nodeId; })->second;
//                        if (matrix->any)
//                        {
//                            vec = vec * matrix->matrix;
//                        }
//                        dataNewPosition.push_back(vec.x);
//                        dataNewPosition.push_back(vec.y);
//                        dataNewPosition.push_back(vec.z);
//                    }
//
//                    // Save positions to buffer, bufferview, and accessor
//                    bufferBuilder.AddBufferView(BufferViewTarget::ARRAY_BUFFER);
//                    vector<float> minValues(3U, numeric_limits<float>::max());
//                    vector<float> maxValues(3U, numeric_limits<float>::lowest());
//                    const size_t positionCount = dataNewPosition.size();
//                    for (size_t i = 0U, j = 0U; i < positionCount; ++i, j = (i % 3U))
//                    {
//                        minValues[j] = min(dataNewPosition[i], minValues[j]);
//                        maxValues[j] = max(dataNewPosition[i], maxValues[j]);
//                    }
//                    accessorIdPositons = bufferBuilder.AddAccessor(dataNewPosition, { accessorPosition.type, accessorPosition.componentType, false, move(minValues), move(maxValues) }).id;
//                }
//
//                // Indices
//                const Accessor& accessorIndices = document.accessors.Get(meshPrimitive.indicesAccessorId);
//                const auto dataIndices = gltfResourceReader->ReadBinaryData<uint16_t>(document, accessorIndices);
//                bufferBuilder.AddBufferView(BufferViewTarget::ELEMENT_ARRAY_BUFFER);
//                string accessorIdIndices = bufferBuilder.AddAccessor(dataIndices, { accessorIndices.type, accessorIndices.componentType }).id;
//
//                // Mesh Primitive
//                MeshPrimitive meshPrimitive;
//                meshPrimitive.indicesAccessorId = accessorIdIndices;
//                meshPrimitive.attributes[ACCESSOR_POSITION] = accessorIdPositons;
//                meshFinal.primitives.push_back(move(meshPrimitive));
//            }
//        }
//    }
//
//    return meshFinal;
//}
