#include "hunter.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>

namespace vh {

const char* slotFolder(Slot s) {
    switch (s) {
    case Slot::Helm: return "helm";
    case Slot::Body: return "body";
    case Slot::Arm: return "arm";
    case Slot::Waist: return "wst";
    case Slot::Leg: return "leg";
    }
    return "body";
}

const char* slotLabel(Slot s) {
    switch (s) {
    case Slot::Helm: return "Head";
    case Slot::Body: return "Chest";
    case Slot::Arm: return "Arms";
    case Slot::Waist: return "Waist";
    case Slot::Leg: return "Legs";
    }
    return "Chest";
}

Vec3 HunterSkeleton::pos(int function) const {
    int b = bone(function);
    if (b < 0) throw std::runtime_error("hunter skeleton lacks bone function " + std::to_string(function));
    return world[size_t(b)].translation();
}

namespace {

bool isPhysics(int f) { return f >= pf::PhysicsFirst && f <= pf::PhysicsLast; }

struct Source {
    int tmpl;
    int index;
    Mat4 world;
};

} // namespace

HunterSkeleton buildHunterSkeleton(const std::vector<Mod3File>& templates) {
    if (templates.empty()) throw std::runtime_error("no template models");
    std::map<int, Source> byFunction;
    for (size_t t = 0; t < templates.size(); ++t) {
        const Mod3File& f = templates[t];
        for (size_t i = 0; i < f.bones.size(); ++i) {
            int fn = f.bones[i].function;
            if (fn < 0 || fn >= 512 || isPhysics(fn) || byFunction.count(fn)) continue;
            byFunction[fn] = Source{int(t), int(i), f.worldMatrix(int(i))};
        }
    }
    if (!byFunction.count(pf::Head) || !byFunction.count(pf::Pelvis))
        throw std::runtime_error("template models do not contain a hunter skeleton");

    // parent function for each kept bone (nearest kept ancestor)
    std::map<int, int> parentFn;
    for (auto& kv : byFunction) {
        const Mod3File& f = templates[size_t(kv.second.tmpl)];
        int p = f.bones[size_t(kv.second.index)].parent;
        int pfn = -1;
        int guard = 0;
        while (p != 255 && p < int(f.bones.size()) && guard++ < 512) {
            int candidate = f.bones[size_t(p)].function;
            auto it = byFunction.find(candidate);
            if (it != byFunction.end()) { pfn = candidate; break; }
            p = f.bones[size_t(p)].parent;
        }
        parentFn[kv.first] = pfn;
    }

    // topological order: depth first from roots, children sorted by function id
    std::map<int, std::vector<int>> children;
    std::vector<int> roots;
    for (auto& kv : parentFn) {
        if (kv.second < 0) roots.push_back(kv.first);
        else children[kv.second].push_back(kv.first);
    }
    std::vector<int> order;
    std::function<void(int)> visit = [&](int fn) {
        order.push_back(fn);
        for (int c : children[fn]) visit(c);
    };
    for (int r : roots) visit(r);
    if (order.size() > 255) throw std::runtime_error("merged skeleton has too many bones");

    std::map<int, int> indexOf;
    for (size_t i = 0; i < order.size(); ++i) indexOf[order[i]] = int(i);

    HunterSkeleton hs;
    hs.base = templates[0];
    hs.base.bones.clear();
    hs.base.lmats.clear();
    hs.base.amats.clear();
    hs.base.meshes.clear();
    hs.base.materials.clear();
    for (int fn : order) {
        const Source& s = byFunction[fn];
        const Mod3File& f = templates[size_t(s.tmpl)];
        Mod3Bone b = f.bones[size_t(s.index)];
        int pfn = parentFn[fn];
        Mat4 local = pfn < 0 ? s.world : byFunction[pfn].world.inverse() * s.world;
        b.parent = pfn < 0 ? 255 : uint8_t(indexOf[pfn]);
        int sym = b.symmetric;
        b.symmetric = 255;
        if (sym != 255 && sym < int(f.bones.size())) {
            auto it = indexOf.find(f.bones[size_t(sym)].function);
            if (it != indexOf.end()) b.symmetric = uint8_t(it->second);
        }
        Vec3 t = local.translation();
        b.x = t.x; b.y = t.y; b.z = t.z;
        b.length = length(t);
        hs.base.bones.push_back(b);
        hs.base.lmats.push_back(local);
        hs.world.push_back(s.world);
        hs.base.amats.push_back(s.world.inverse());
    }
    hs.base.rebuildBoneMap();

    // mesh defaults and vertex conventions from the first template's largest skinned mesh
    const Mod3Mesh* best = nullptr;
    for (const auto& t : templates)
        for (const auto& m : t.meshes) {
            const VertexLayout* L = findLayout(m.h.blocktype);
            if (!L || L->weights == 0) continue;
            if (!best || m.vertices.size() > best->vertices.size()) best = &m;
        }
    if (best) {
        hs.meshDefaults = best->h;
        std::map<int, int> nw, tw;
        for (const auto& v : best->vertices) {
            nw[v.normal[3]]++;
            tw[std::abs(int(v.tangent[3]))]++;
        }
        auto mostCommon = [](const std::map<int, int>& m, int fallback) {
            int bestK = fallback, bestC = -1;
            for (auto& kv : m) if (kv.second > bestC) { bestC = kv.second; bestK = kv.first; }
            return bestK;
        };
        hs.normalW = int8_t(mostCommon(nw, 0));
        hs.tangentWPositive = std::clamp(mostCommon(tw, 127), 1, 127);
    }
    hs.meshDefaults.lod = 0xFFFF; // visible at every level of detail

    // measure winding and tangent space conventions on the templates' triangles
    double windVotes = 0, bitVotes = 0;
    for (const auto& t : templates)
        for (const auto& m : t.meshes) {
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                if (m.indices[i] >= m.vertices.size() || m.indices[i + 1] >= m.vertices.size() || m.indices[i + 2] >= m.vertices.size()) continue;
                const Mod3Vertex* v[3] = {&m.vertices[m.indices[i]], &m.vertices[m.indices[i + 1]], &m.vertices[m.indices[i + 2]]};
                Vec3 e1 = v[1]->pos - v[0]->pos, e2 = v[2]->pos - v[0]->pos;
                Vec3 geo = cross(e1, e2);
                if (length(geo) < 1e-8f) continue;
                Vec3 n;
                for (auto* x : v) n += Vec3{x->normal[0] / 127.0f, x->normal[1] / 127.0f, x->normal[2] / 127.0f};
                float d = dot(normalize(geo), normalize(n, {0, 0, 0}));
                if (std::fabs(d) > 0.3f) windVotes += d > 0 ? 1 : -1;
                // tangent frame
                float du1 = v[1]->uv[0][0] - v[0]->uv[0][0], dv1 = v[1]->uv[0][1] - v[0]->uv[0][1];
                float du2 = v[2]->uv[0][0] - v[0]->uv[0][0], dv2 = v[2]->uv[0][1] - v[0]->uv[0][1];
                float det = du1 * dv2 - du2 * dv1;
                if (std::fabs(det) < 1e-10f) continue;
                Vec3 dPdv = (e2 * du1 - e1 * du2) * (1.0f / det);
                Vec3 nn{v[0]->normal[0] / 127.0f, v[0]->normal[1] / 127.0f, v[0]->normal[2] / 127.0f};
                Vec3 tt{v[0]->tangent[0] / 127.0f, v[0]->tangent[1] / 127.0f, v[0]->tangent[2] / 127.0f};
                float w = v[0]->tangent[3] < 0 ? -1.0f : 1.0f;
                float b = dot(normalize(dPdv, {0, 0, 0}), cross(nn, tt) * w);
                if (std::fabs(b) > 0.3f) bitVotes += b > 0 ? 1 : -1;
            }
        }
    if (windVotes != 0) hs.frontFaceSign = windVotes > 0 ? 1 : -1;
    if (bitVotes != 0) hs.bitangentSign = bitVotes > 0 ? 1 : -1;
    return hs;
}

HunterSkeleton syntheticHunterSkeleton() {
    // Rough hunter proportions in centimetres, Y up, facing +Z, character left = +X,
    // arms in a relaxed A-pose. Used only when no game data is present.
    struct B { int fn; int parent; Vec3 p; };
    const float armDrop = 0.6f; // A-pose: arm direction (cos, -sin)
    auto arm = [&](float side, float along, float yBase) {
        return Vec3{side * (16.0f + along * std::cos(armDrop)), yBase - along * std::sin(armDrop), -2.0f};
    };
    std::vector<B> bs = {
        {pf::Root, -1, {0, 0, 0}},
        {pf::Pelvis, pf::Root, {0, 98, 0}},
        {pf::Waist, pf::Pelvis, {0, 108, 0}},
        {pf::Chest, pf::Waist, {0, 126, 0}},
        {pf::Neck, pf::Chest, {0, 146, -1}},
        {pf::Head, pf::Neck, {0, 155, 0}},
        {pf::ShoulderL, pf::Chest, {4, 141, -2}},
        {pf::UpperArmL, pf::ShoulderL, arm(1, 0, 141)},
        {pf::ForearmL, pf::UpperArmL, arm(1, 28, 141)},
        {pf::HandL, pf::ForearmL, arm(1, 53, 141)},
        {pf::ShoulderR, pf::Chest, {-4, 141, -2}},
        {pf::UpperArmR, pf::ShoulderR, arm(-1, 0, 141)},
        {pf::ForearmR, pf::UpperArmR, arm(-1, 28, 141)},
        {pf::HandR, pf::ForearmR, arm(-1, 53, 141)},
        {pf::ThighL, pf::Pelvis, {9, 94, 0}},
        {pf::CalfL, pf::ThighL, {9, 52, 1}},
        {pf::FootL, pf::CalfL, {9, 9, -2}},
        {pf::ToeL, pf::FootL, {9, 2, 12}},
        {pf::ThighR, pf::Pelvis, {-9, 94, 0}},
        {pf::CalfR, pf::ThighR, {-9, 52, 1}},
        {pf::FootR, pf::CalfR, {-9, 9, -2}},
        {pf::ToeR, pf::FootR, {-9, 2, 12}},
    };
    // fingers: 5 chains of 3 per hand, laid out along the arm direction
    const int fingerBase[2][5] = {{pf::ThumbAL, pf::IndexAL, pf::MiddleAL, pf::RingAL, pf::PinkyAL},
                                  {pf::ThumbAR, pf::IndexAR, pf::MiddleAR, pf::RingAR, pf::PinkyAR}};
    for (int s = 0; s < 2; ++s) {
        float side = s == 0 ? 1.0f : -1.0f;
        int hand = s == 0 ? pf::HandL : pf::HandR;
        for (int f = 0; f < 5; ++f) {
            float zOff = (f == 0) ? 3.0f : 2.0f - f * 1.3f;
            float startAlong = f == 0 ? 55.0f : 61.0f;
            int parent = hand;
            for (int k = 0; k < 3; ++k) {
                Vec3 p = arm(side, startAlong + k * 2.5f, 141);
                p.z += zOff;
                bs.push_back({fingerBase[s][f] + k, parent, p});
                parent = fingerBase[s][f] + k;
            }
        }
    }
    Mod3File f;
    std::map<int, int> idx;
    for (size_t i = 0; i < bs.size(); ++i) idx[bs[i].fn] = int(i);
    for (const auto& b : bs) {
        Mod3Bone mb;
        mb.function = int16_t(b.fn);
        mb.parent = b.parent < 0 ? 255 : uint8_t(idx[b.parent]);
        Vec3 parentPos = b.parent < 0 ? Vec3{0, 0, 0} : bs[size_t(idx[b.parent])].p;
        Vec3 t = b.p - parentPos;
        mb.x = t.x; mb.y = t.y; mb.z = t.z;
        mb.length = length(t);
        f.bones.push_back(mb);
        f.lmats.push_back(Mat4::fromLinearTranslation(Mat3::identity(), t));
    }
    for (size_t i = 0; i < f.bones.size(); ++i) f.amats.push_back(f.worldMatrix(int(i)).inverse());
    f.rebuildBoneMap();
    return buildHunterSkeleton({f});
}

} // namespace vh
