// glTF-Interpolater.cpp : This file contains the 'main' function. Program execution begins and ends there.

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
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cassert>
#include <cstdlib>

using namespace std;
using namespace Microsoft::glTF;

int main()
{
    std::experimental::filesystem::path sourcePath = "D:/Code/glTF-SDK/glTF-Interpolater/Data";
    std::experimental::filesystem::path filename = "Buggy.gltf";
    std::experimental::filesystem::path TargetFilename = "MergedMesh_Buggy.gltf";
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

    cout << "glTF loaded!";

    auto streamWriter = make_unique<StreamWriter>(sourcePath);
    auto resourceWriter = make_unique<GLTFResourceWriter>(move(streamWriter));

    Document documentTarget;
    BufferBuilder bufferBuilder(move(resourceWriter));

    const char* bufferId = "MergedMesh_Buggy";
    bufferBuilder.AddBuffer(bufferId);

    // Rebuild the model as a single mesh.
    Mesh meshTarget;
    for (const auto& mesh : documentSource.meshes.Elements())
    {
        for (const auto& meshPrimitiveSource : mesh.primitives)
        {
            const Accessor& indicesAccessor = documentSource.accessors.Get(meshPrimitiveSource.indicesAccessorId);
            const auto indicesData = gltfResourceReader->ReadBinaryData<uint16_t>(documentSource, indicesAccessor);
            bufferBuilder.AddBufferView(BufferViewTarget::ELEMENT_ARRAY_BUFFER);
            string accessorIdIndices = bufferBuilder.AddAccessor(indicesData, { indicesAccessor.type, indicesAccessor.componentType }).id;

            //string accessorId;
            //if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_POSITION, accessorId))
            //{
            //    const Accessor& accessor = documentSource.accessors.Get(accessorId);
            //    const auto data = gltfResourceReader->ReadBinaryData<float>(documentSource, accessor);

            //}

            MeshPrimitive meshPrimitiveTarget;
            meshPrimitiveTarget.indicesAccessorId = accessorIdIndices;
            meshTarget.primitives.push_back(move(meshPrimitiveTarget));
        }
    }
    bufferBuilder.Output(documentTarget);

    auto meshId = documentTarget.meshes.Append(move(meshTarget), AppendIdPolicy::GenerateOnEmpty).id;

    Node node;
    node.meshId = meshId;
    auto nodeId = documentTarget.nodes.Append(move(node), AppendIdPolicy::GenerateOnEmpty).id;

    Scene scene;
    scene.nodes.push_back(nodeId);
    documentTarget.SetDefaultScene(move(scene), AppendIdPolicy::GenerateOnEmpty);

    // Bake each node's (and its parent's) transforms into its mesh primitive's vertex positions.


    // Remove material and Texture Coords from all primitives


    // Add each primitive to a new mesh in a new node


    // Discard all textures, images, and texture samplers. (no materials)
    // Discard all skins. (Targets a node and won't work the same with a combined mesh. Loss of potential transforms?)
    // Discard all nodes that don't have a camera attached. (transforms baked into positions. Morph won't work the same with only one mesh)
    // Discard all animation channels that target non-camera nodes. (animations won't work the same with only one mesh.)
    // Discard all animations without channels. (previously discarded channels will reveal which animations are no longer used.)
    // (mesh weights are dropped automatically by using a new base mesh)

    // Write the model to a new file.
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



    //for (const auto& mesh : documentSource.meshes.Elements())
    //{
    //    cout << "Mesh: " << mesh.id << "\n";

    //    for (const auto& meshPrimitive : mesh.primitives)
    //    {
    //        string accessorId;
    //        if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_POSITION, accessorId))
    //        {
    //            const Accessor& accessor = documentSource.accessors.Get(accessorId);

    //            const auto data = gltfResourceReader->ReadBinaryData<float>(documentSource, accessor);
    //            cout << "Position: " << "\n" << '[';
    //            int index = 0;
    //            for (auto i = data.begin(); i != data.end(); i++)
    //            {
    //                cout << *i;
    //                if ((index + 1) % 3 == 0)
    //                {
    //                    cout << "]\n[";
    //                }
    //                else
    //                {
    //                    cout << ", ";
    //                }
    //                index++;
    //            }
    //        }
    //    }
    //}
}
