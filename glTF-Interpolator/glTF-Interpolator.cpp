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
#include <sstream>
#include <cassert>
#include <cstdlib>

using namespace std;
using namespace Microsoft::glTF;
using namespace Eigen;

int main()
{
    string sourcePath = "D:/Code/glTF-SDK/glTF-Interpolator/Data";
    string filename = "buggy.gltf";//"Node_Attribute_08.gltf";
    string TargetFilename = "MergedMesh.gltf";

    // Load the glTF model
    auto streamReader = make_unique<StreamReader>(sourcePath);
    auto gltfStream = streamReader->GetInputStream(filename);
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

    // Build a mutable structure to track an manipulate the node transforms.
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
    cout << "Transforms loaded!\n";

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
                childMatrix->second = childMatrix->second * modifiedMatrices[index].second;
            }
            else
            {
                childMatrix->second = modifiedMatrices[index].second;
            }
        }
        index++;
    }
    cout << "Transforms flattened!\n";

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
                    // Load positions
                    const Accessor& accessorPosition = documentSource.accessors.Get(accessorIdPositons);
                    const auto dataPosition = gltfResourceReader->ReadBinaryData<float>(documentSource, accessorPosition);
                    vector<float> dataNewPosition;
                    // Transform positions
                    for (auto it = dataPosition.begin(); it != dataPosition.end(); it++)
                    {
                        // Rebuild the positions as a Vector4.
                        auto x = *it;
                        it++;
                        auto y = *it;
                        it++;
                        auto z = *it;
                        Vector4f vec(x, y, z, 1);

                        // Bakes the node's transforms into the positions.
                        auto &nodeId = node.id;
                        auto matrix = find_if(modifiedMatrices.begin(), modifiedMatrices.end(), [nodeId](const pair<string, Matrix4f>& obj) { return obj.first == nodeId; })->second;
                        vec = vec.transpose() * matrix;

                        dataNewPosition.push_back(vec.x());
                        dataNewPosition.push_back(vec.y());
                        dataNewPosition.push_back(vec.z());
                    }

                    // Save positions to buffer, bufferview, and accessor.
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
    gltfResourceWriter.WriteExternal(TargetFilename, manifestTarget);

    cout << "glTF written to new file!\n";

    //TODO:
    // Apply a material.
    // Save the node with the camera, and its transform.
    // Save the camera.
    // Improve performance.
    // Garbage collection?
    // Use a relative path.
    // Removed unneeded #include statements
}
