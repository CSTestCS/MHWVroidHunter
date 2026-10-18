// Fits a VRM avatar onto the hunter skeleton.
//
// 1. The VRM is rotated into the hunter's coordinate frame (detected from the skeleton itself,
//    so no assumption about the game's axis conventions is needed).
// 2. It is uniformly scaled so its ankle-to-head height equals the hunter's.
// 3. Every humanoid bone is rotated onto the matching hunter bone direction and, depending on
//    `conform`, stretched along the bone so the joints land on the hunter's joints. With
//    conform = 1 the VRM body follows the hunter's proportions exactly, which is what makes the
//    in-game armour line up with it.
// 4. Skin weights are moved onto the hunter's bones and the mesh is split into the five armour
//    slots (head, chest, arms, waist, legs).
#pragma once

#include <string>
#include <vector>

#include "hunter.h"
#include "vrm.h"

namespace vh {

struct RetargetOptions {
    float conform = 1.0f;          // 0 = keep VRM proportions, 1 = snap joints to hunter joints
    bool dropTransparent = true;   // remove triangles that are fully transparent in their texture
};

struct OutVertex {
    Vec3 pos;
    Vec3 normal;
    Vec4 tangent;
    Vec2 uv;
    uint8_t bones[4] = {0, 0, 0, 0};
    float weights[4] = {1, 0, 0, 0};
};

struct OutMesh {
    Slot slot = Slot::Body;
    int material = 0; // VRM material index
    std::vector<OutVertex> vertices;
    std::vector<uint32_t> indices;
};

struct RetargetReport {
    float scale = 1;            // VRM metres -> hunter units
    float hunterHeight = 0;     // ankle to head, hunter units
    float vrmHeight = 0;        // ankle to head, metres
    bool mirroredAxes = false;  // the game's coordinate system has opposite handedness to glTF
    int droppedTriangles = 0;
    int mappedBones = 0;
    std::vector<std::string> warnings;
};

struct RetargetResult {
    std::vector<OutMesh> meshes; // each has <= 65535 vertices
    RetargetReport report;
};

RetargetResult retargetToHunter(const VrmModel& vrm, const HunterSkeleton& hunter, const RetargetOptions& opt);

// Output meshes use cross(b - a, c - a) . normal > 0 for front faces and the glTF bitangent
// rule. This flips triangles and tangent signs where the game's measured conventions differ.
void matchGameConventions(std::vector<OutMesh>& meshes, int frontFaceSign, int bitangentSign);
// Measured conventions of converted meshes (same definition as HunterSkeleton's fields).
void measureConventions(const std::vector<OutMesh>& meshes, int& frontFaceSign, int& bitangentSign);

} // namespace vh
