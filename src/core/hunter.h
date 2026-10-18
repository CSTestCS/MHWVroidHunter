// The hunter (player) skeleton, built from the game's own armour models.
#pragma once

#include <string>
#include <vector>

#include "mod3.h"

namespace vh {

// Player bone function ids (MHW modding wiki, "3d Model Bone Function Tables").
namespace pf {
enum : int {
    Root = 0, Waist = 1, Chest = 2, Neck = 3, Head = 4,
    ShoulderL = 5, UpperArmL = 6, ForearmL = 7, HandL = 8,
    ShoulderR = 9, UpperArmR = 10, ForearmR = 11, HandR = 12,
    Pelvis = 13,
    ThighL = 14, CalfL = 15, FootL = 16, ToeL = 17,
    ThighR = 18, CalfR = 19, FootR = 20, ToeR = 21,
    // fingers: A/B/C = proximal/intermediate/distal
    ThumbAL = 31, IndexAL = 34, MiddleAL = 37, RingAL = 41, PinkyAL = 44,
    ThumbAR = 48, IndexAR = 51, MiddleAR = 54, RingAR = 58, PinkyAR = 61,
    PhysicsFirst = 150, PhysicsLast = 199,
};
}

enum class Slot { Helm = 0, Body = 1, Arm = 2, Waist = 3, Leg = 4 };
constexpr int kSlotCount = 5;
const char* slotFolder(Slot s);   // "helm", "body", "arm", "wst", "leg"
const char* slotLabel(Slot s);    // user facing name

struct HunterSkeleton {
    Mod3File base;              // header/segments/groups copied from the body template, skeleton = merged
    std::vector<Mat4> world;    // world matrix per bone (index aligned with base.bones)
    Mod3MeshHeader meshDefaults; // mesh header fields taken from a template skinned mesh
    int8_t normalW = 0;         // observed normal.w value used by the game's meshes
    int tangentWPositive = 127; // observed tangent.w magnitude
    // Conventions measured on the game's own meshes (+1 / -1):
    // frontFaceSign: sign of dot(cross(b - a, c - a), vertex normal) for a front facing triangle.
    // bitangentSign: sign of dot(dP/dv, cross(normal, tangent) * tangent.w).
    int frontFaceSign = 1;
    int bitangentSign = 1;

    int bone(int function) const { return base.boneByFunction(function); }
    bool has(int function) const { return bone(function) >= 0; }
    Vec3 pos(int function) const;
};

// Merges the skeletons of several template models (e.g. helm/body/arm/wst/leg of one armour set).
// The first model provides the file header data. Physics bones are dropped because they belong to
// specific armour pieces.
HunterSkeleton buildHunterSkeleton(const std::vector<Mod3File>& templates);

// Creates a synthetic hunter-like skeleton. Only used by tests and the offline preview when no game
// files are available; the real converter always uses the game's skeleton.
HunterSkeleton syntheticHunterSkeleton();

} // namespace vh
