// Loads a .vrm avatar (glTF 2.0 binary with the VRM 0.x or VRMC_vrm 1.0 extension)
// into a flat, rest-pose representation that is easy to retarget.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "tex.h"
#include "vh_math.h"

namespace vh {

struct VrmNode {
    std::string name;
    int parent = -1;
    Mat4 world; // rest pose world transform
};

struct VrmVertex {
    Vec3 pos;      // world space, rest pose, metres, glTF axes
    Vec3 normal;
    Vec4 tangent;  // w = handedness
    Vec2 uv;
    int joints[4] = {-1, -1, -1, -1}; // node indices
    float weights[4] = {0, 0, 0, 0};
};

enum class AlphaMode { Opaque, Mask, Blend };

struct VrmMaterial {
    std::string name;
    int image = -1;           // index into VrmModel::images, -1 = none
    Vec4 baseColor{1, 1, 1, 1};
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct VrmPrimitive {
    int material = -1;
    std::vector<VrmVertex> vertices;
    std::vector<uint32_t> indices;
};

struct VrmModel {
    int vrmVersion = 0; // 0 = VRM 0.x, 1 = VRM 1.0
    std::string title;
    std::vector<VrmNode> nodes;
    std::map<std::string, int> humanBones; // VRM 1.0 style humanoid name -> node index
    std::vector<VrmPrimitive> primitives;
    std::vector<VrmMaterial> materials;
    std::vector<Image> images;

    int human(const std::string& bone) const {
        auto it = humanBones.find(bone);
        return it == humanBones.end() ? -1 : it->second;
    }
    Vec3 jointPos(int node) const { return nodes.at(size_t(node)).world.translation(); }
};

// Throws std::runtime_error with a user-readable message on failure.
VrmModel loadVrm(const std::string& path);

} // namespace vh
