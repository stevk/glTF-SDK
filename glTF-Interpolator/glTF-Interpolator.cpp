#include "pch.h"
#include "StreamReader.h"
#include "StreamWriter.h"
#include <GLTFSDK/Deserialize.h>
#include <GLTFSDK/Serialize.h>
#include <GLTFSDK/GLTFResourceWriter.h>
#include <GLTFSDK/GLTFResourceReader.h>
#include <GLTFSDK/BufferBuilder.h>
#include <Eigen/Dense>
#include <iostream>

using namespace std;
using namespace Microsoft::glTF;
using namespace Eigen;

int main()
{
    auto sourcePath = experimental::filesystem::current_path() / "Data";

    string filename = "buggy.gltf";
    //string filename = "Node_Attribute_08.gltf";
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
    // The index is the node ID, while the matrix is the transformation matrix.
    vector<Matrix4f> modifiedMatrices;
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

        modifiedMatrices.push_back(matrix);
    }
    cout << "Transforms loaded!\n";

    // Flatten the matrix transforms, so every node's matrix also includes the transforms of its parents.
    int index = 0;
    for (const auto& node : documentSource.nodes.Elements())
    {
        for (auto it = node.children.begin(); it != node.children.end(); it++)
        {
            modifiedMatrices[stoi(*it)] = modifiedMatrices[stoi(*it)] * modifiedMatrices[index];
        }
        index++;
    }
    cout << "Transforms flattened!\n";

    // Material
    Material material;
    material.emissiveFactor = Color3(0.0f, 0.0f, 0.0f);
    material.metallicRoughness.baseColorFactor = Color4(1.0f, 1.0f, 1.0f, 1.0f);
    material.metallicRoughness.metallicFactor = 0.0f;
    auto materialId = documentTarget.materials.Append(move(material), AppendIdPolicy::GenerateOnEmpty).id;

    // Rebuild the meshes as a single mesh.
    Mesh meshTarget;
    string nodeWithCameraId = "";
    for (const auto& node : documentSource.nodes.Elements())
    {
        // Note that this means a mesh can be called more than once, if it is instanced by more than one node.
        if (node.meshId != "")
        {
            auto mesh = documentSource.meshes.Get(node.meshId);
            for (const auto& meshPrimitive : mesh.primitives)
            {
                // Normal
                string accessorIdNormal = "";
                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_NORMAL, accessorIdNormal))
                {
                    // Load normals
                    const Accessor& accessorNormal = documentSource.accessors.Get(accessorIdNormal);
                    const auto dataNormal = gltfResourceReader->ReadBinaryData<float>(documentSource, accessorNormal);

                    // Save positions to buffer, bufferview, and accessor.
                    bufferBuilder.AddBufferView(BufferViewTarget::ARRAY_BUFFER);
                    vector<float> minValues(3U, numeric_limits<float>::max());
                    vector<float> maxValues(3U, numeric_limits<float>::lowest());
                    const size_t countNormal = dataNormal.size();
                    for (size_t i = 0U, j = 0U; i < countNormal; ++i, j = (i % 3U))
                    {
                        minValues[j] = min(dataNormal[i], minValues[j]);
                        maxValues[j] = max(dataNormal[i], maxValues[j]);
                    }
                    accessorIdNormal = bufferBuilder.AddAccessor(dataNormal, { accessorNormal.type, accessorNormal.componentType, false, move(minValues), move(maxValues) }).id;
                }

                // Position
                string accessorIdPositon = "";
                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_POSITION, accessorIdPositon))
                {
                    // Load positions
                    const Accessor& accessorPosition = documentSource.accessors.Get(accessorIdPositon);
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

                        auto matrix = modifiedMatrices[stoi(node.id)];
                        vec = vec.transpose() * matrix;

                        dataNewPosition.push_back(move(vec.x()));
                        dataNewPosition.push_back(move(vec.y()));
                        dataNewPosition.push_back(move(vec.z()));
                    }

                    // Save positions to buffer, bufferview, and accessor.
                    bufferBuilder.AddBufferView(BufferViewTarget::ARRAY_BUFFER);
                    vector<float> minValues(3U, numeric_limits<float>::max());
                    vector<float> maxValues(3U, numeric_limits<float>::lowest());
                    const size_t countPosition = dataNewPosition.size();
                    for (size_t i = 0U, j = 0U; i < countPosition; ++i, j = (i % 3U))
                    {
                        minValues[j] = min(dataNewPosition[i], minValues[j]);
                        maxValues[j] = max(dataNewPosition[i], maxValues[j]);
                    }
                    accessorIdPositon = bufferBuilder.AddAccessor(dataNewPosition, { accessorPosition.type, accessorPosition.componentType, false, move(minValues), move(maxValues) }).id;
                }

                // Indices
                const Accessor& accessorIndices = documentSource.accessors.Get(meshPrimitive.indicesAccessorId);
                const auto dataIndices = gltfResourceReader->ReadBinaryData<uint16_t>(documentSource, accessorIndices);
                bufferBuilder.AddBufferView(BufferViewTarget::ELEMENT_ARRAY_BUFFER);
                string accessorIdIndices = bufferBuilder.AddAccessor(dataIndices, { accessorIndices.type, accessorIndices.componentType }).id;

                // Mesh Primitive (assemble attributes)
                MeshPrimitive meshPrimitiveTarget;
                meshPrimitiveTarget.indicesAccessorId = move(accessorIdIndices);
                meshPrimitiveTarget.attributes[ACCESSOR_NORMAL] = move(accessorIdNormal);
                meshPrimitiveTarget.attributes[ACCESSOR_POSITION] = move(accessorIdPositon);
                meshPrimitiveTarget.materialId = materialId;
                meshPrimitiveTarget.mode = move(meshPrimitive.mode);
                meshTarget.primitives.push_back(move(meshPrimitiveTarget));
            }
        }
        else if (node.cameraId != "")
        {
            // If a camera is attached to a node, it needs to be preserved. 
            // This code won't be called if a mesh is attached to the same node. Multiple cameras would cause issues.
            // Preserves the camera.
            documentTarget.cameras.Append(move(documentSource.cameras.Get(node.cameraId)));

            auto matrix = modifiedMatrices[stoi(node.id)];
            Matrix4 matrixTarget;
            int index = 0;
            for (auto it = matrixTarget.values.begin(); it != matrixTarget.values.end(); it++)
            {
                int row = index / 4;
                int col = index % 4;
                *it = matrix((row), (col));
                index++;
            }

            Node nodeTarget;
            nodeTarget.cameraId = node.cameraId;
            nodeTarget.matrix = move(matrixTarget);
            nodeWithCameraId = documentTarget.nodes.Append(move(nodeTarget), AppendIdPolicy::GenerateOnEmpty).id;
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
    if (nodeWithCameraId != "")
    {
        scene.nodes.push_back(nodeWithCameraId);
    }
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
}
