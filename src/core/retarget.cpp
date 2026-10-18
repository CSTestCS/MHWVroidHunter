#include "retarget.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace vh {

namespace {

struct HumanMap {
    const char* name;   // VRM 1.0 humanoid bone name
    int function;       // hunter bone function
    Slot slot;
    const char* target; // VRM bone that defines this bone's direction (nullptr = inherit)
    int targetFn;       // hunter function that defines the direction
};

// Ordered parents first.
const HumanMap kMap[] = {
    {"hips", pf::Pelvis, Slot::Waist, nullptr, -1},
    {"spine", pf::Waist, Slot::Body, "chest", pf::Chest},
    {"chest", pf::Chest, Slot::Body, "neck", pf::Neck},
    {"neck", pf::Neck, Slot::Body, "head", pf::Head},
    {"head", pf::Head, Slot::Helm, nullptr, -1},

    {"leftShoulder", pf::ShoulderL, Slot::Body, "leftUpperArm", pf::UpperArmL},
    {"leftUpperArm", pf::UpperArmL, Slot::Body, "leftLowerArm", pf::ForearmL},
    {"leftLowerArm", pf::ForearmL, Slot::Arm, "leftHand", pf::HandL},
    {"leftHand", pf::HandL, Slot::Arm, "leftMiddleProximal", pf::MiddleAL},
    {"rightShoulder", pf::ShoulderR, Slot::Body, "rightUpperArm", pf::UpperArmR},
    {"rightUpperArm", pf::UpperArmR, Slot::Body, "rightLowerArm", pf::ForearmR},
    {"rightLowerArm", pf::ForearmR, Slot::Arm, "rightHand", pf::HandR},
    {"rightHand", pf::HandR, Slot::Arm, "rightMiddleProximal", pf::MiddleAR},

    {"leftThumbMetacarpal", pf::ThumbAL, Slot::Arm, "leftThumbProximal", pf::ThumbAL + 1},
    {"leftThumbProximal", pf::ThumbAL + 1, Slot::Arm, "leftThumbDistal", pf::ThumbAL + 2},
    {"leftThumbDistal", pf::ThumbAL + 2, Slot::Arm, nullptr, -1},
    {"leftIndexProximal", pf::IndexAL, Slot::Arm, "leftIndexIntermediate", pf::IndexAL + 1},
    {"leftIndexIntermediate", pf::IndexAL + 1, Slot::Arm, "leftIndexDistal", pf::IndexAL + 2},
    {"leftIndexDistal", pf::IndexAL + 2, Slot::Arm, nullptr, -1},
    {"leftMiddleProximal", pf::MiddleAL, Slot::Arm, "leftMiddleIntermediate", pf::MiddleAL + 1},
    {"leftMiddleIntermediate", pf::MiddleAL + 1, Slot::Arm, "leftMiddleDistal", pf::MiddleAL + 2},
    {"leftMiddleDistal", pf::MiddleAL + 2, Slot::Arm, nullptr, -1},
    {"leftRingProximal", pf::RingAL, Slot::Arm, "leftRingIntermediate", pf::RingAL + 1},
    {"leftRingIntermediate", pf::RingAL + 1, Slot::Arm, "leftRingDistal", pf::RingAL + 2},
    {"leftRingDistal", pf::RingAL + 2, Slot::Arm, nullptr, -1},
    {"leftLittleProximal", pf::PinkyAL, Slot::Arm, "leftLittleIntermediate", pf::PinkyAL + 1},
    {"leftLittleIntermediate", pf::PinkyAL + 1, Slot::Arm, "leftLittleDistal", pf::PinkyAL + 2},
    {"leftLittleDistal", pf::PinkyAL + 2, Slot::Arm, nullptr, -1},

    {"rightThumbMetacarpal", pf::ThumbAR, Slot::Arm, "rightThumbProximal", pf::ThumbAR + 1},
    {"rightThumbProximal", pf::ThumbAR + 1, Slot::Arm, "rightThumbDistal", pf::ThumbAR + 2},
    {"rightThumbDistal", pf::ThumbAR + 2, Slot::Arm, nullptr, -1},
    {"rightIndexProximal", pf::IndexAR, Slot::Arm, "rightIndexIntermediate", pf::IndexAR + 1},
    {"rightIndexIntermediate", pf::IndexAR + 1, Slot::Arm, "rightIndexDistal", pf::IndexAR + 2},
    {"rightIndexDistal", pf::IndexAR + 2, Slot::Arm, nullptr, -1},
    {"rightMiddleProximal", pf::MiddleAR, Slot::Arm, "rightMiddleIntermediate", pf::MiddleAR + 1},
    {"rightMiddleIntermediate", pf::MiddleAR + 1, Slot::Arm, "rightMiddleDistal", pf::MiddleAR + 2},
    {"rightMiddleDistal", pf::MiddleAR + 2, Slot::Arm, nullptr, -1},
    {"rightRingProximal", pf::RingAR, Slot::Arm, "rightRingIntermediate", pf::RingAR + 1},
    {"rightRingIntermediate", pf::RingAR + 1, Slot::Arm, "rightRingDistal", pf::RingAR + 2},
    {"rightRingDistal", pf::RingAR + 2, Slot::Arm, nullptr, -1},
    {"rightLittleProximal", pf::PinkyAR, Slot::Arm, "rightLittleIntermediate", pf::PinkyAR + 1},
    {"rightLittleIntermediate", pf::PinkyAR + 1, Slot::Arm, "rightLittleDistal", pf::PinkyAR + 2},
    {"rightLittleDistal", pf::PinkyAR + 2, Slot::Arm, nullptr, -1},

    {"leftUpperLeg", pf::ThighL, Slot::Leg, "leftLowerLeg", pf::CalfL},
    {"leftLowerLeg", pf::CalfL, Slot::Leg, "leftFoot", pf::FootL},
    {"leftFoot", pf::FootL, Slot::Leg, "leftToes", pf::ToeL},
    {"leftToes", pf::ToeL, Slot::Leg, nullptr, -1},
    {"rightUpperLeg", pf::ThighR, Slot::Leg, "rightLowerLeg", pf::CalfR},
    {"rightLowerLeg", pf::CalfR, Slot::Leg, "rightFoot", pf::FootR},
    {"rightFoot", pf::FootR, Slot::Leg, "rightToes", pf::ToeR},
    {"rightToes", pf::ToeR, Slot::Leg, nullptr, -1},
};
constexpr int kMapCount = int(sizeof(kMap) / sizeof(kMap[0]));

// Per mapped bone deformation: x' = P + R * (x - p + a * (k - 1) * dot(a, x - p))
struct BoneXform {
    bool active = false;
    int node = -1;      // VRM node
    int hunterBone = 0; // output bone index
    Slot slot = Slot::Body;
    Vec3 p;             // VRM joint (already in hunter frame)
    Vec3 P;             // fitted joint
    Mat3 R;
    Vec3 a{0, 1, 0};
    float k = 1;

    Vec3 point(const Vec3& x) const {
        Vec3 d = x - p;
        d += a * ((k - 1) * dot(a, d));
        return P + R * d;
    }
    Vec3 normal(const Vec3& n) const {
        // inverse transpose of (I + (k-1) a a^T) is (I + (1/k - 1) a a^T)
        Vec3 d = n + a * ((1.0f / k - 1) * dot(a, n));
        return R * d;
    }
    Vec3 tangent(const Vec3& t) const { return R * (t + a * ((k - 1) * dot(a, t))); }
};

float alphaAt(const VrmModel& vrm, const VrmMaterial& m, const Vec2& uv) {
    float a = m.baseColor.w;
    if (m.image >= 0 && m.image < int(vrm.images.size())) {
        float px[4];
        vrm.images[size_t(m.image)].sample(uv.x, uv.y, px);
        a *= px[3] / 255.0f;
    }
    return a;
}

Vec3 orthoNormal(const Vec3& v, const Vec3& against) { return normalize(v - against * dot(v, against)); }

} // namespace

RetargetResult retargetToHunter(const VrmModel& vrm, const HunterSkeleton& hunter, const RetargetOptions& opt) {
    RetargetResult result;
    RetargetReport& rep = result.report;
    const float conform = clamp01(opt.conform);

    auto vj = [&](const char* name) { return vrm.jointPos(vrm.human(name)); };
    auto hj = [&](int fn) { return hunter.pos(fn); };

    for (int fn : {pf::Pelvis, pf::Head, pf::ThighL, pf::ThighR, pf::FootL, pf::FootR, pf::UpperArmL, pf::UpperArmR})
        if (!hunter.has(fn)) throw std::runtime_error("The game's hunter skeleton is missing a required bone (" + std::to_string(fn) + ").");

    // ---------------------------------------------------------------- global frames
    // glTF is right handed, +Y up, and the character's left is up x forward.
    Vec3 Uv{0, 1, 0};
    Vec3 Lv = orthoNormal(vj("leftUpperLeg") - vj("rightUpperLeg"), Uv);
    Vec3 Fv = cross(Lv, Uv);

    Vec3 ankleH = (hj(pf::FootL) + hj(pf::FootR)) * 0.5f;
    Vec3 Uh = normalize(hj(pf::Head) - ankleH);
    Vec3 Lh = orthoNormal(hj(pf::ThighL) - hj(pf::ThighR), Uh);
    Vec3 Fh = cross(Lh, Uh);
    if (hunter.has(pf::ToeL) && hunter.has(pf::ToeR)) {
        Vec3 toes = (hj(pf::ToeL) + hj(pf::ToeR)) * 0.5f - ankleH;
        toes = toes - Uh * dot(toes, Uh);
        if (length(toes) > 1e-4f && dot(toes, Fh) < 0) {
            Fh = -Fh; // the game's axes have the opposite handedness to glTF
            rep.mirroredAxes = true;
        }
    }
    const Mat3 Bv = Mat3::fromColumns(Lv, Uv, Fv);
    const Mat3 Bh = Mat3::fromColumns(Lh, Uh, Fh);
    const Mat3 M = Bh * Bv.transposed();
    const float det = M.det() < 0 ? -1.0f : 1.0f;

    Vec3 ankleV = (vj("leftFoot") + vj("rightFoot")) * 0.5f;
    rep.vrmHeight = dot(vj("head") - ankleV, Uv);
    rep.hunterHeight = dot(hj(pf::Head) - ankleH, Uh);
    if (rep.vrmHeight <= 1e-4f || rep.hunterHeight <= 1e-4f) throw std::runtime_error("Could not measure the model height (is the VRM upside down?).");
    const float s = rep.hunterHeight / rep.vrmHeight;
    rep.scale = s;
    const Mat3 G = Mat3::fromColumns(M.c[0] * s, M.c[1] * s, M.c[2] * s);
    auto toHunter = [&](const Vec3& p) { return G * (p - ankleV) + ankleH; };

    // ---------------------------------------------------------------- bone fitting
    std::map<int, int> nodeToMap; // VRM node -> kMap index
    std::vector<BoneXform> xf(kMapCount);
    for (int i = 0; i < kMapCount; ++i) {
        int node = vrm.human(kMap[i].name);
        if (node < 0 || !hunter.has(kMap[i].function)) continue;
        xf[size_t(i)].active = true;
        xf[size_t(i)].node = node;
        xf[size_t(i)].hunterBone = hunter.bone(kMap[i].function);
        xf[size_t(i)].slot = kMap[i].slot;
        xf[size_t(i)].p = toHunter(vrm.jointPos(node));
        nodeToMap[node] = i;
        rep.mappedBones++;
    }
    auto mappedAncestor = [&](int node, bool includeSelf) {
        int n = includeSelf ? node : vrm.nodes[size_t(node)].parent;
        while (n >= 0) {
            auto it = nodeToMap.find(n);
            if (it != nodeToMap.end()) return it->second;
            n = vrm.nodes[size_t(n)].parent;
        }
        return -1;
    };
    const int hipsIdx = 0;
    for (int i = 0; i < kMapCount; ++i) {
        BoneXform& b = xf[size_t(i)];
        if (!b.active) continue;
        int parent = i == hipsIdx ? -1 : mappedAncestor(b.node, false);
        if (i != hipsIdx && parent < 0) parent = hipsIdx;
        const Vec3 h = hunter.pos(kMap[i].function);
        if (parent < 0) {
            // hips: align the pelvis frame (up = towards the spine, side = between the thighs)
            Vec3 upV = toHunter(vj("spine")) - b.p;
            Vec3 sideV = toHunter(vj("leftUpperLeg")) - toHunter(vj("rightUpperLeg"));
            int upFn = hunter.has(pf::Waist) && length(hj(pf::Waist) - h) > 1e-3f ? pf::Waist : pf::Chest;
            Vec3 upH = hj(upFn) - h;
            Vec3 sideH = hj(pf::ThighL) - hj(pf::ThighR);
            Mat3 fv = basisFrom(upV, sideV), fh = basisFrom(upH, sideH);
            b.R = fh * fv.transposed();
            b.P = lerp(b.p, h, conform);
            b.k = 1;
            continue;
        }
        const BoneXform& pb = xf[size_t(parent)];
        Vec3 predicted = pb.point(b.p);
        b.P = lerp(predicted, h, conform);
        b.R = pb.R;
        b.k = 1;
        int targetNode = kMap[i].target ? vrm.human(kMap[i].target) : -1;
        // spine/chest may skip optional VRM bones
        if (targetNode < 0 && kMap[i].function == pf::Waist) targetNode = vrm.human("neck");
        if (targetNode < 0 && kMap[i].function == pf::Chest) targetNode = vrm.human("head");
        if (targetNode >= 0 && kMap[i].targetFn >= 0 && hunter.has(kMap[i].targetFn)) {
            Vec3 dv = toHunter(vrm.jointPos(targetNode)) - b.p;
            Vec3 dh = hunter.pos(kMap[i].targetFn) - h;
            if (length(dv) > 1e-4f && length(dh) > 1e-4f) {
                b.R = rotationBetween(pb.R * normalize(dv), normalize(dh)) * pb.R;
                b.a = normalize(dv);
                float ratio = length(dh) / length(dv);
                b.k = 1 + (std::clamp(ratio, 0.25f, 4.0f) - 1) * conform;
            }
        }
    }

    // ---------------------------------------------------------------- mesh conversion
    std::map<int, int> ownerCache;
    auto ownerOf = [&](int node) {
        auto it = ownerCache.find(node);
        if (it != ownerCache.end()) return it->second;
        int m = node >= 0 ? mappedAncestor(node, true) : -1;
        if (m < 0) m = hipsIdx;
        ownerCache[node] = m;
        return m;
    };

    struct Converted {
        OutVertex v;
        float slotWeight[kSlotCount] = {0, 0, 0, 0, 0};
    };
    auto convertVertex = [&](const VrmVertex& src) {
        Converted c;
        std::map<int, float> acc; // kMap index -> weight
        for (int k = 0; k < 4; ++k)
            if (src.weights[k] > 0 && src.joints[k] >= 0) acc[ownerOf(src.joints[k])] += src.weights[k];
        if (acc.empty()) acc[hipsIdx] = 1;
        std::vector<std::pair<float, int>> ws;
        for (auto& kv : acc) ws.push_back({kv.second, kv.first});
        std::sort(ws.begin(), ws.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        if (ws.size() > 4) ws.resize(4);
        float total = 0;
        for (auto& w : ws) total += w.first;
        const Vec3 gp = toHunter(src.pos), gn = M * src.normal, gt = M * src.tangent.xyz();
        Vec3 pos, nrm, tan;
        for (size_t k = 0; k < ws.size(); ++k) {
            const float w = ws[k].first / total;
            const BoneXform& b = xf[size_t(ws[k].second)];
            pos += b.point(gp) * w;
            nrm += b.normal(gn) * w;
            tan += b.tangent(gt) * w;
            c.v.bones[k] = uint8_t(b.hunterBone);
            c.v.weights[k] = w;
            c.slotWeight[int(b.slot)] += w;
        }
        for (size_t k = ws.size(); k < 4; ++k) {
            c.v.bones[k] = c.v.bones[0];
            c.v.weights[k] = 0;
        }
        c.v.pos = pos;
        c.v.normal = normalize(nrm);
        const Vec3 t = normalize(tan - c.v.normal * dot(tan, c.v.normal), {1, 0, 0});
        c.v.tangent = {t.x, t.y, t.z, src.tangent.w * det};
        c.v.uv = src.uv;
        return c;
    };

    struct Bucket {
        OutMesh mesh;
        std::map<uint64_t, uint32_t> remap; // (primitive, vertex, back side) -> local index
    };
    std::vector<Bucket> out;
    std::map<std::pair<int, int>, size_t> current; // (slot, material) -> index into out

    for (size_t pi = 0; pi < vrm.primitives.size(); ++pi) {
        const VrmPrimitive& prim = vrm.primitives[pi];
        const VrmMaterial& mat = vrm.materials.at(size_t(prim.material));
        std::vector<Converted> cv;
        cv.reserve(prim.vertices.size());
        for (const VrmVertex& v : prim.vertices) cv.push_back(convertVertex(v));

        const bool cutout = mat.alphaMode != AlphaMode::Opaque && opt.dropTransparent;
        const float cutoff = mat.alphaMode == AlphaMode::Mask ? mat.alphaCutoff : 0.05f;
        for (size_t t = 0; t + 2 < prim.indices.size(); t += 3) {
            uint32_t tri[3] = {prim.indices[t], prim.indices[t + 1], prim.indices[t + 2]};
            if (tri[0] >= cv.size() || tri[1] >= cv.size() || tri[2] >= cv.size()) continue;
            if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2]) continue;
            if (cutout) {
                const Vec2 &a = prim.vertices[tri[0]].uv, &b = prim.vertices[tri[1]].uv, &c = prim.vertices[tri[2]].uv;
                const Vec2 samples[7] = {a, b, c,
                                         {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3},
                                         {(a.x + b.x) / 2, (a.y + b.y) / 2},
                                         {(b.x + c.x) / 2, (b.y + c.y) / 2},
                                         {(a.x + c.x) / 2, (a.y + c.y) / 2}};
                bool visible = false;
                for (const Vec2& uv : samples) visible = visible || alphaAt(vrm, mat, uv) >= cutoff;
                if (!visible) {
                    rep.droppedTriangles++;
                    continue;
                }
            }
            float sw[kSlotCount] = {0, 0, 0, 0, 0};
            for (uint32_t i : tri)
                for (int sl = 0; sl < kSlotCount; ++sl) sw[sl] += cv[i].slotWeight[sl];
            int slot = int(std::max_element(sw, sw + kSlotCount) - sw);

            const int passes = mat.doubleSided ? 2 : 1;
            for (int pass = 0; pass < passes; ++pass) {
                const bool back = pass == 1;
                auto key = std::make_pair(slot, prim.material);
                auto it = current.find(key);
                if (it == current.end() || out[it->second].mesh.vertices.size() + 3 > 65535) {
                    out.push_back(Bucket{});
                    out.back().mesh.slot = Slot(slot);
                    out.back().mesh.material = prim.material;
                    current[key] = out.size() - 1;
                    it = current.find(key);
                }
                Bucket& bk = out[it->second];
                uint32_t local[3];
                for (int k = 0; k < 3; ++k) {
                    uint64_t id = (uint64_t(pi) << 33) | (uint64_t(tri[k]) << 1) | (back ? 1u : 0u);
                    auto f = bk.remap.find(id);
                    if (f != bk.remap.end()) {
                        local[k] = f->second;
                    } else {
                        OutVertex v = cv[tri[k]].v;
                        if (back) {
                            v.normal = -v.normal;
                            v.tangent.w = -v.tangent.w;
                        }
                        local[k] = uint32_t(bk.mesh.vertices.size());
                        bk.mesh.vertices.push_back(v);
                        bk.remap[id] = local[k];
                    }
                }
                // glTF front faces are counter-clockwise; a mirrored axis change or the back
                // side of a double-sided material reverses the winding.
                bool flip = (det < 0) != back;
                if (flip) std::swap(local[1], local[2]);
                bk.mesh.indices.insert(bk.mesh.indices.end(), local, local + 3);
            }
        }
    }
    for (auto& b : out)
        if (!b.mesh.indices.empty()) result.meshes.push_back(std::move(b.mesh));

    if (rep.mappedBones < 15) rep.warnings.push_back("Only a few humanoid bones could be matched; the model may deform badly.");
    if (s < 30.0f || s > 300.0f)
        rep.warnings.push_back("Unusual size ratio between the VRM and the hunter (" + std::to_string(s) + "x).");
    return result;
}

void measureConventions(const std::vector<OutMesh>& meshes, int& frontFaceSign, int& bitangentSign) {
    double wind = 0, bit = 0;
    for (const auto& m : meshes)
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const OutVertex& a = m.vertices[m.indices[i]];
            const OutVertex& b = m.vertices[m.indices[i + 1]];
            const OutVertex& c = m.vertices[m.indices[i + 2]];
            Vec3 e1 = b.pos - a.pos, e2 = c.pos - a.pos, geo = cross(e1, e2);
            if (length(geo) < 1e-8f) continue;
            float d = dot(normalize(geo), normalize(a.normal + b.normal + c.normal, {0, 0, 0}));
            if (std::fabs(d) > 0.3f) wind += d > 0 ? 1 : -1;
            float du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y, du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;
            float det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) < 1e-10f) continue;
            Vec3 dPdv = (e2 * du1 - e1 * du2) * (1.0f / det);
            float s = dot(normalize(dPdv, {0, 0, 0}), cross(a.normal, a.tangent.xyz()) * (a.tangent.w < 0 ? -1.0f : 1.0f));
            if (std::fabs(s) > 0.3f) bit += s > 0 ? 1 : -1;
        }
    frontFaceSign = wind >= 0 ? 1 : -1;
    bitangentSign = bit >= 0 ? 1 : -1;
}

void matchGameConventions(std::vector<OutMesh>& meshes, int frontFaceSign, int bitangentSign) {
    int ours = 1, oursBit = 1;
    measureConventions(meshes, ours, oursBit);
    if (ours != frontFaceSign)
        for (auto& m : meshes)
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) std::swap(m.indices[i + 1], m.indices[i + 2]);
    if (oursBit != bitangentSign)
        for (auto& m : meshes)
            for (auto& v : m.vertices) v.tangent.w = -v.tangent.w;
}

} // namespace vh
