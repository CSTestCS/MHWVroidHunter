// MT Framework / Monster Hunter World .mod3 model files (version 237).
// Layout follows the community documentation (AsteriskAmpersand's Mod3 importer,
// CrazyT's 010 templates and the MHW modding wiki).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "vh_math.h"

namespace vh {

#pragma pack(push, 1)
struct Mod3Bone {
    int16_t function = 0;     // bone function / remap id used by animations
    uint8_t parent = 255;     // index of parent bone, 255 = none
    uint8_t symmetric = 255;  // index of mirrored bone, 255 = none
    float unkn2 = 0;
    float length = 0;
    float x = 0, y = 0, z = 0; // local translation (same as LMatrix translation)
};
static_assert(sizeof(Mod3Bone) == 24, "Mod3Bone layout");

struct Mod3MeshHeader {
    uint16_t shadowCast = 19;
    uint16_t vertexCount = 0;
    uint16_t visibleCondition = 0;
    uint16_t materialIdx = 0;
    int32_t lod = 0xFFFF;
    uint16_t weightDynamics = 0;
    uint8_t blockSize = 0;
    uint8_t unkn3 = 0;
    uint32_t vertexSub = 0;
    uint32_t vertexOffset = 0;
    uint32_t blocktype = 0;
    uint32_t faceOffset = 0;   // in uint16 units from the start of the face buffer
    uint32_t faceCount = 0;    // number of indices
    uint32_t vertexBase = 0;
    uint8_t null0 = 0;
    uint8_t boundingBoxCount = 0;
    uint16_t unknownIndex = 0;
    uint16_t vertexSubMirror = 0;
    uint16_t vertexIndexSub = 0;
    int16_t mapData[2] = {-1, -1};
    int32_t null1 = 0;
    int32_t intUnknown = 0;
    uint32_t vertexSubTotal = 0;
    int32_t null2[4] = {0, 0, 0, 0};
};
static_assert(sizeof(Mod3MeshHeader) == 80, "Mod3MeshHeader layout");

struct Mod3BoundingBox {
    int32_t boneIndex = 0;
    uint8_t spacer[12] = {0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD};
    float center[3] = {0, 0, 0};
    float radius = 0;
    float aabbMin[4] = {0, 0, 0, 0};
    float aabbMax[4] = {0, 0, 0, 0};
    float oabbMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float oabbVector[4] = {0, 0, 0, 0};
};
static_assert(sizeof(Mod3BoundingBox) == 144, "Mod3BoundingBox layout");
#pragma pack(pop)

struct VertexLayout {
    uint32_t blocktype;
    const char* name;
    int uvs;
    int weights; // 0, 4 or 8
    bool colour;
    int blockSize() const { return 12 + 4 + 4 + uvs * 4 + (weights == 4 ? 8 : weights == 8 ? 16 : 0) + (colour ? 4 : 0); }
};
const VertexLayout* findLayout(uint32_t blocktype);
const VertexLayout* findLayout(int uvs, int weights, bool colour);

struct Mod3Vertex {
    Vec3 pos;
    int8_t normal[4] = {0, 127, 0, 0};
    int8_t tangent[4] = {127, 0, 0, 127};
    float uv[4][2] = {};
    uint8_t bones[8] = {};
    float weights[8] = {}; // full weights including the implicit complement weight
    uint8_t colour[4] = {0, 0, 0, 255};
};

struct Mod3Mesh {
    Mod3MeshHeader h;
    std::vector<Mod3Vertex> vertices;
    std::vector<uint16_t> indices; // local to this mesh's vertex list
    std::vector<Mod3BoundingBox> boxes;
};

struct Mod3File {
    uint8_t version = 237;
    uint8_t version2 = 0;
    uint32_t vertexIds = 0;
    uint32_t secondBufferSize = 0;
    uint64_t creationDate = 0;
    uint64_t unkOffset2 = 0;

    float boundCenter[3] = {0, 0, 0};
    float boundRadius = 0;
    float boundMin[4] = {0, 0, 0, 0};
    float boundMax[4] = {0, 0, 0, 0};
    std::array<uint8_t, 96> floatSegment{};
    std::array<uint8_t, 64> byteSegment{};

    std::vector<Mod3Bone> bones;
    std::vector<Mat4> lmats; // local (relative to parent)
    std::vector<Mat4> amats; // inverse of absolute (world) matrix
    std::array<uint8_t, 512> boneMap{}; // function id -> bone index (255 = unused)

    std::vector<std::array<uint8_t, 32>> groups;
    std::vector<std::string> materials;
    std::vector<Mod3Mesh> meshes;
    std::vector<uint8_t> trailing;

    static Mod3File parse(const std::vector<uint8_t>& data);
    std::vector<uint8_t> serialize() const;

    // Recomputes the 512-entry bone map from bone functions.
    void rebuildBoneMap();
    // Recomputes header bounding data from all mesh vertices.
    void computeBounds();
    // World (absolute) matrix of a bone, computed from the local matrix chain.
    Mat4 worldMatrix(int bone) const;
    int boneByFunction(int function) const;
};

// Builds per-bone bounding boxes for a mesh (vertices expressed in bone space).
std::vector<Mod3BoundingBox> computeMeshBoxes(const Mod3Mesh& mesh, const std::vector<Mat4>& worldMatrices);

} // namespace vh
