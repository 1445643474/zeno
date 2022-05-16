#include "assimp/scene.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"

#include <zeno/zeno.h>
#include <zeno/core/IObject.h>
#include <zeno/types/StringObject.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/DictObject.h>
#include <zeno/utils/logger.h>
#include <zeno/extra/GlobalState.h>

#include <stack>
#include <string>
#include <unordered_map>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <stb_image.h>

#include "Definition.h"

struct Mesh{
    FBXData fbxData;
    std::vector<Texture> textures_loaded;
    std::unordered_map<std::string, std::vector<unsigned int>> m_VerticesSlice;
    std::unordered_map<std::string, aiMatrix4x4> m_TransMatrix;
    unsigned int m_VerticesIncrease = 0;
    unsigned int m_IndicesIncrease = 0;

    void initMesh(const aiScene *scene){
        m_VerticesIncrease = 0;

        readTrans(scene->mRootNode, aiMatrix4x4());
        processNode(scene->mRootNode, scene);
    }

    void readTrans(const aiNode * parentNode, aiMatrix4x4 parentTransform){
        unsigned int childrenCount = parentNode->mNumChildren;
        aiMatrix4x4 transformation = parentNode->mTransformation;

        std::string name( parentNode->mName.data ) ;
        if (m_TransMatrix.find(name) == m_TransMatrix.end() && parentNode->mNumMeshes){
            m_TransMatrix[name] = aiMatrix4x4();
        }

        transformation = parentTransform * transformation;

        if(parentNode->mNumMeshes){
            m_TransMatrix[name] = transformation * m_TransMatrix[name];
        }

        for (int i = 0; i < childrenCount; i++) {
            readTrans(parentNode->mChildren[i], transformation);
        }
    }

    void processMesh(aiMesh *mesh, const aiScene *scene) {
        std::string meshName(mesh->mName.data);
        std::vector<Texture> textures;

        // Vertices
        for(unsigned int j = 0; j < mesh->mNumVertices; j++){
            VertexInfo vertexInfo;

            aiVector3D vec(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);
            vertexInfo.position = vec;

            if (mesh->mTextureCoords[0]){
                aiVector3D uvw(mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y, 0.0f);
                vertexInfo.texCoord = uvw;
            }
            if (mesh->mNormals) {
                aiVector3D normal(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z);
                vertexInfo.normal = normal;
            }
            if(mesh->mTangents){
                aiVector3D tangent(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z);
                vertexInfo.tangent = tangent;
            }
            if(mesh->mBitangents){
                aiVector3D bitangent(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z);
                vertexInfo.bitangent = bitangent;
            }

            fbxData.vertices.push_back(vertexInfo);
        }

        // Indices
        for(unsigned int j = 0; j < mesh->mNumFaces; j++)
        {
            aiFace face = mesh->mFaces[j];
            for(unsigned int j = 0; j < face.mNumIndices; j++)
                fbxData.indices.push_back(face.mIndices[j] + m_VerticesIncrease);
        }

        // Material
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
//        zeno::log_info("MatTex: Mesh {} Material {}", meshName, material->GetName().data);
        std::vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());

        // Bone
        extractBone(mesh);

        m_VerticesSlice[meshName] = std::vector<unsigned int>
                {static_cast<unsigned int>(m_VerticesIncrease),  // Vert Start
                 m_VerticesIncrease + mesh->mNumVertices,  // Vert End
                 mesh->mNumBones,
                 m_IndicesIncrease,  // Indices Start
                 static_cast<unsigned int>(m_IndicesIncrease + (fbxData.indices.size() - m_IndicesIncrease))  // Indices End
                 };

        m_IndicesIncrease += (fbxData.indices.size() - m_IndicesIncrease);
        m_VerticesIncrease += mesh->mNumVertices;
    }

    void processNode(aiNode *node, const aiScene *scene){

        //zeno::log_info("Node: Name {}", node->mName.data);
        for(unsigned int i = 0; i < node->mNumMeshes; i++)
            processMesh(scene->mMeshes[node->mMeshes[i]], scene);
        for(unsigned int i = 0; i < node->mNumChildren; i++)
            processNode(node->mChildren[i], scene);
    }

    std::vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, std::string typeName)
    {
        std::vector<Texture> textures;
        for(unsigned int i = 0; i < mat->GetTextureCount(type); i++)
        {
            aiString str;
            mat->GetTexture(type, i, &str);
//            zeno::log_info("MatTex: Texture Name {}", str.data);

            bool skip = false;
            for(unsigned int j = 0; j < textures_loaded.size(); j++)
            {
                if(std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0)  // Compare two strings
                {
                    textures.push_back(textures_loaded[j]);
                    skip = true;
                    break;
                }
            }
            if(! skip)
            {
                Texture texture;
                texture.id;
                texture.type = typeName;
                texture.path = str.C_Str();
                textures.push_back(texture);
                textures_loaded.push_back(texture);
            }
        }
        return textures;
    }

    void extractBone(aiMesh* mesh){
        for (int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
        {
            std::string boneName(mesh->mBones[boneIndex]->mName.C_Str());

            // Not Found, Create one, If Found, will have same offset-matrix
            if (fbxData.BoneOffsetMap.find(boneName) == fbxData.BoneOffsetMap.end()) {
                BoneInfo newBoneInfo;

                newBoneInfo.name = boneName;
                newBoneInfo.offset = mesh->mBones[boneIndex]->mOffsetMatrix;

                fbxData.BoneOffsetMap[boneName] = newBoneInfo;
            }

            auto weights = mesh->mBones[boneIndex]->mWeights;
            unsigned int numWeights = mesh->mBones[boneIndex]->mNumWeights;
            for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
            {
                int vertexId = weights[weightIndex].mVertexId + m_VerticesIncrease;
                float weight = weights[weightIndex].mWeight;

                auto& vertex = fbxData.vertices[vertexId];
                vertex.boneWeights[boneName] = weight;
            }
        }
    }

    void processTrans(std::unordered_map<std::string, Bone>& bones,
                      std::shared_ptr<zeno::DictObject>& prims,
                      std::shared_ptr<zeno::DictObject>& datas) {
        for(auto& iter: m_VerticesSlice) {
            std::string meshName = iter.first;
            std::vector<unsigned int> verSlice = iter.second;
            unsigned int verStart = verSlice[0];
            unsigned int verEnd = verSlice[1];
            unsigned int verBoneNum = verSlice[2];
            unsigned int indicesStart = verSlice[3];
            unsigned int indicesEnd = verSlice[4];

            // TODO full support blend bone-animation and mesh-animation, See SimTrans.fbx
            bool foundMeshBone = bones.find(meshName) != bones.end();
            /*
            int meshBoneId = -1;
            float meshBoneWeight = 0.0f;
            if(foundMeshBone){
                meshBoneId = boneInfo[meshName].id;
                meshBoneWeight = 1.0f;
            }
             */

            for(unsigned int i=verStart; i<verEnd; i++){
                if(verBoneNum == 0){
                    auto & vertex = fbxData.vertices[i];

                    if(foundMeshBone){
                        //vertex.boneIds[0] = meshBoneId;
                        //vertex.boneWeights[0] = meshBoneWeight;
                    }else
                    {
                        vertex.position = m_TransMatrix[meshName] * fbxData.vertices[i].position;
                    }
                }
            }

            // Sub-prims (applied node transform)
            auto sub_prim = std::make_shared<zeno::PrimitiveObject>();
            auto sub_data = std::make_shared<FBXData>();
            std::vector<VertexInfo> sub_vertices;
            std::vector<unsigned int> sub_indices;

            for(unsigned int i=indicesStart; i<indicesEnd; i+=3){
                auto i1 = fbxData.indices[i]-verStart;
                auto i2 = fbxData.indices[i+1]-verStart;
                auto i3 = fbxData.indices[i+2]-verStart;
                zeno::vec3i incs(i1, i2, i3);
                sub_prim->tris.push_back(incs);
                sub_indices.push_back(i1);
                sub_indices.push_back(i2);
                sub_indices.push_back(i3);
            }
            for(unsigned int i=verStart; i< verEnd; i++){
                sub_prim->verts.emplace_back(fbxData.vertices[i].position.x,
                                             fbxData.vertices[i].position.y,
                                             fbxData.vertices[i].position.z);
                sub_vertices.push_back(fbxData.vertices[i]);
            }
            sub_data->indices = sub_indices;
            sub_data->vertices = sub_vertices;
            sub_data->BoneOffsetMap = fbxData.BoneOffsetMap;

            prims->lut[meshName] = sub_prim;
            datas->lut[meshName] = sub_data;
        }
    }

    void processPrim(std::shared_ptr<zeno::PrimitiveObject>& prim){
        auto &ver = prim->verts;
        auto &ind = prim->tris;
        auto &uv = prim->verts.add_attr<zeno::vec3f>("uv");
        auto &norm = prim->verts.add_attr<zeno::vec3f>("nrm");

        for(unsigned int i=0; i<fbxData.vertices.size(); i++){
            auto& vpos = fbxData.vertices[i].position;
            auto& vnor = fbxData.vertices[i].normal;
            auto& vuv = fbxData.vertices[i].texCoord;
            ver.emplace_back(vpos.x, vpos.y, vpos.z);
            uv.emplace_back(vuv.x, vuv.y, vuv.z);
            norm.emplace_back(vnor.x, vnor.y, vnor.z);
        }

        for(unsigned int i=0; i<fbxData.indices.size(); i+=3){
            zeno::vec3i incs(fbxData.indices[i],
                             fbxData.indices[i+1],
                             fbxData.indices[i+2]);
            ind.push_back(incs);
        }
    }
};

struct Anim{
    NodeTree m_RootNode;
    BoneTree m_Bones;

    float duration;
    float tick;
    void initAnim(aiScene const*scene, Mesh* model){

        readHierarchyData(m_RootNode, scene->mRootNode);
        //zeno::log_info("----- Anim: Convert AssimpNode.");

        if(scene->mNumAnimations){
            // TODO handle more animation if have
            auto animation = scene->mAnimations[0];
            duration = animation->mDuration;
            tick = animation->mTicksPerSecond;

            setupBones(animation, model);
        }
    }

    void readHierarchyData(NodeTree &dest, const aiNode *src) {
        dest.name = std::string(src->mName.data);
        dest.transformation = src->mTransformation;
        dest.childrenCount = src->mNumChildren;

        //zeno::log_info("Tree: Name {}", dest.name);

        for (int i = 0; i < src->mNumChildren; i++) {
            NodeTree newData;
            readHierarchyData(newData, src->mChildren[i]);
            dest.children.push_back(newData);
        }
    }

    void setupBones(const aiAnimation *animation, Mesh* model) {
        int size = animation->mNumChannels;
        //zeno::log_info("SetupBones: Num Channels {}", size);

        for (int i = 0; i < size; i++) {
            auto channel = animation->mChannels[i];
            std::string boneName(channel->mNodeName.data);
            //zeno::log_info("----- Name {}", boneName);

            Bone bone;
            bone.initBone(boneName, channel);

            m_Bones.AnimBoneMap[boneName] = bone;
        }

        zeno::log_info("SetupBones: Num AnimBoneMap: {}", m_Bones.AnimBoneMap.size());
    }

};

void readFBXFile(
        std::shared_ptr<zeno::PrimitiveObject>& prim,
        std::shared_ptr<zeno::DictObject>& prims,
        std::shared_ptr<zeno::DictObject>& datas,
        std::shared_ptr<NodeTree>& nodeTree,
        std::shared_ptr<FBXData>& fbxData,
        std::shared_ptr<BoneTree>& boneTree,
        std::shared_ptr<AnimInfo>& animInfo,
        const char *fbx_path)
{
    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_PTV_NORMALIZE, true);
    aiScene const* scene = importer.ReadFile(fbx_path,
                                             aiProcess_Triangulate
                                             | aiProcess_FlipUVs
                                             | aiProcess_CalcTangentSpace
                                             | aiProcess_JoinIdenticalVertices);
    if(! scene)
        zeno::log_error("ReadFBXPrim: Invalid assimp scene");

    Mesh mesh;
    Anim anim;

    mesh.initMesh(scene);
    anim.initAnim(scene, &mesh);
    mesh.processTrans(anim.m_Bones.AnimBoneMap, prims, datas);
    mesh.processPrim(prim);

    *fbxData = mesh.fbxData;
    *nodeTree = anim.m_RootNode;
    *boneTree = anim.m_Bones;

    animInfo->duration = anim.duration;
    animInfo->tick = anim.tick;

//    zeno::log_info("ReadFBXPrim: Num Animation {}", scene->mNumAnimations);
//    zeno::log_info("ReadFBXPrim: Vertices count {}", mesh.fbxData.vertices.size());
//    zeno::log_info("ReadFBXPrim: Indices count {}", mesh.fbxData.indices.size());
//    zeno::log_info("ReadFBXPrim: readFBXFile done.");
}

struct ReadFBXPrim : zeno::INode {

    virtual void apply() override {
        auto path = get_input<zeno::StringObject>("path")->get();
        auto prim = std::make_shared<zeno::PrimitiveObject>();
        std::shared_ptr<zeno::DictObject> prims = std::make_shared<zeno::DictObject>();
        std::shared_ptr<zeno::DictObject> datas = std::make_shared<zeno::DictObject>();
        auto nodeTree = std::make_shared<NodeTree>();
        auto animInfo = std::make_shared<AnimInfo>();
        auto fbxData = std::make_shared<FBXData>();
        auto boneTree = std::make_shared<BoneTree>();

        zeno::log_info("ReadFBXPrim: File Path {}", path);

        readFBXFile(prim,prims, datas,
                    nodeTree, fbxData, boneTree, animInfo,
                    path.c_str());

        set_output("prim", std::move(prim));
        set_output("primdict", std::move(prims));
        set_output("datadict", std::move(datas));
        set_output("animinfo", std::move(animInfo));
        set_output("nodetree", std::move(nodeTree));
        set_output("fbxdata", std::move(fbxData));
        set_output("bonetree", std::move(boneTree));
    }
};

ZENDEFNODE(ReadFBXPrim,
           {       /* inputs: */
               {
                   {"readpath", "path"},
                   {"frameid"}
               },  /* outputs: */
               {
                   "prim", "primdict", "datadict",
                   {"AnimInfo", "animinfo"},
                   {"FBXData", "fbxdata"},
                   {"NodeTree", "nodetree"},
                   {"BoneTree", "bonetree"},
               },  /* params: */
               {

               },  /* category: */
               {
                   "primitive",
               }
           });
