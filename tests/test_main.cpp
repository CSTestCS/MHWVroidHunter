// Self-contained tests for the converter. They do not need game files: a tiny VRM, fake game
// template models and a fake encrypted archive are generated on the fly.
//
// Optional: set VH_TEST_VRM to a real .vrm file to also run the full pipeline on it.
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <stb_image_write.h>
#include <string>
#include <vector>

#include "binio.h"
#include "autoconvert.h"
#include "builder.h"
#include "models.h"
#include "chunk.h"
#include "fsutil.h"
#include "hunter.h"
#include "mod3.h"
#include "mrl3.h"
#include "retarget.h"
#include "tex.h"
#include "vrm.h"

using namespace vh;
using json = nlohmann::json;

static int g_failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "  CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

static std::string tmpDir(const std::string& name) {
    std::string d = std::string("/tmp/vh_test_") + name;
    std::filesystem::remove_all(d);
    makeDirs(d);
    return d;
}

// ---------------------------------------------------------------- synthetic VRM
// A box-person in T-pose, 1.6 m tall, VRM 0.x (facing -Z) or VRM 1.0 (facing +Z).
static std::string writeTestVrm(const std::string& path, int version) {
    struct N { const char* human; int parent; Vec3 p; };
    // positions for a character facing +Z with its left at +X (VRM 1.0 convention)
    std::vector<N> nodes = {
        {"hips", -1, {0, 0.9f, 0}},          {"spine", 0, {0, 1.0f, 0}},        {"chest", 1, {0, 1.15f, 0}},
        {"neck", 2, {0, 1.38f, 0}},          {"head", 3, {0, 1.45f, 0}},        {"leftShoulder", 2, {0.03f, 1.33f, 0}},
        {"leftUpperArm", 5, {0.10f, 1.33f, 0}}, {"leftLowerArm", 6, {0.35f, 1.33f, 0}}, {"leftHand", 7, {0.58f, 1.33f, 0}},
        {"rightShoulder", 2, {-0.03f, 1.33f, 0}}, {"rightUpperArm", 9, {-0.10f, 1.33f, 0}}, {"rightLowerArm", 10, {-0.35f, 1.33f, 0}},
        {"rightHand", 11, {-0.58f, 1.33f, 0}}, {"leftUpperLeg", 0, {0.08f, 0.85f, 0}}, {"leftLowerLeg", 13, {0.08f, 0.47f, 0}},
        {"leftFoot", 14, {0.08f, 0.08f, 0}}, {"leftToes", 15, {0.08f, 0.02f, 0.10f}}, {"rightUpperLeg", 0, {-0.08f, 0.85f, 0}},
        {"rightLowerLeg", 17, {-0.08f, 0.47f, 0}}, {"rightFoot", 18, {-0.08f, 0.08f, 0}}, {"rightToes", 19, {-0.08f, 0.02f, 0.10f}},
        {"leftMiddleProximal", 8, {0.66f, 1.33f, 0}}, {"rightMiddleProximal", 12, {-0.66f, 1.33f, 0}},
        {nullptr, 4, {0, 1.6f, -0.05f}}, // hair bone (non-humanoid)
    };
    const float flip = version == 0 ? -1.0f : 1.0f; // VRM 0.x faces -Z: rotate 180 degrees about Y
    for (auto& n : nodes) { n.p.x *= flip; n.p.z *= flip; }

    // geometry: a box around every bone segment, skinned to that bone
    std::vector<float> pos, nrm, uv;
    std::vector<uint16_t> joints;
    std::vector<float> weights;
    std::vector<uint16_t> idx;
    auto addBox = [&](Vec3 c, Vec3 half, int joint) {
        const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        const Vec3 fn[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        for (int f = 0; f < 6; ++f) {
            uint16_t base = uint16_t(pos.size() / 3);
            for (int k = 0; k < 4; ++k) {
                int v = faces[f][k];
                Vec3 p{c.x + ((v & 4) ? half.x : -half.x), c.y + ((v & 2) ? half.y : -half.y), c.z + ((v & 1) ? half.z : -half.z)};
                pos.insert(pos.end(), {p.x, p.y, p.z});
                nrm.insert(nrm.end(), {fn[f].x, fn[f].y, fn[f].z});
                uv.insert(uv.end(), {k == 1 || k == 2 ? 0.5f : 0.0f, k >= 2 ? 0.5f : 0.0f});
                joints.insert(joints.end(), {uint16_t(joint), 0, 0, 0});
                weights.insert(weights.end(), {1, 0, 0, 0});
            }
            // winding chosen so that the face normal points outwards (CCW seen from outside)
            Vec3 a{pos[size_t(base) * 3], pos[size_t(base) * 3 + 1], pos[size_t(base) * 3 + 2]};
            Vec3 b{pos[size_t(base + 1) * 3], pos[size_t(base + 1) * 3 + 1], pos[size_t(base + 1) * 3 + 2]};
            Vec3 d{pos[size_t(base + 2) * 3], pos[size_t(base + 2) * 3 + 1], pos[size_t(base + 2) * 3 + 2]};
            bool ccw = dot(cross(b - a, d - a), fn[f]) > 0;
            if (ccw) idx.insert(idx.end(), {base, uint16_t(base + 1), uint16_t(base + 2), base, uint16_t(base + 2), uint16_t(base + 3)});
            else idx.insert(idx.end(), {base, uint16_t(base + 2), uint16_t(base + 1), base, uint16_t(base + 3), uint16_t(base + 2)});
        }
    };
    for (size_t i = 0; i < nodes.size(); ++i) {
        int parent = nodes[i].parent;
        if (parent < 0) continue;
        Vec3 a = nodes[size_t(parent)].p, b = nodes[i].p;
        Vec3 c = (a + b) * 0.5f;
        Vec3 half{std::fabs(b.x - a.x) * 0.5f + 0.03f, std::fabs(b.y - a.y) * 0.5f + 0.03f, std::fabs(b.z - a.z) * 0.5f + 0.03f};
        addBox(c, half, parent);
    }
    const size_t opaqueIndexCount = idx.size();
    // a fully transparent quad (should be dropped) using the second material
    {
        uint16_t base = uint16_t(pos.size() / 3);
        for (int k = 0; k < 4; ++k) {
            pos.insert(pos.end(), {0.2f * (k & 1), 1.5f + 0.2f * (k >> 1), 0.2f * flip});
            nrm.insert(nrm.end(), {0, 0, flip});
            uv.insert(uv.end(), {0.75f + 0.2f * (k & 1), 0.75f + 0.2f * (k >> 1)});
            joints.insert(joints.end(), {4, 0, 0, 0});
            weights.insert(weights.end(), {1, 0, 0, 0});
        }
        idx.insert(idx.end(), {base, uint16_t(base + 1), uint16_t(base + 3), base, uint16_t(base + 3), uint16_t(base + 2)});
    }

    // 8x8 texture: top-left quadrant red, bottom-right transparent
    std::vector<uint8_t> tex(8 * 8 * 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            uint8_t* p = &tex[size_t(y * 8 + x) * 4];
            bool tl = x < 4 && y < 4, br = x >= 4 && y >= 4;
            p[0] = tl ? 255 : 40; p[1] = tl ? 0 : 160; p[2] = 40; p[3] = br ? 0 : 255;
        }
    std::vector<uint8_t> png;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    }, &png, 8, 8, 4, tex.data(), 32);

    // binary buffer
    std::vector<uint8_t> bin;
    auto append = [&](const void* p, size_t n) {
        while (bin.size() % 4) bin.push_back(0);
        size_t off = bin.size();
        bin.insert(bin.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
        return off;
    };
    json j;
    j["asset"] = {{"version", "2.0"}};
    j["buffers"] = json::array();
    j["bufferViews"] = json::array();
    j["accessors"] = json::array();
    auto view = [&](const void* p, size_t n) {
        size_t off = append(p, n);
        j["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", off}, {"byteLength", n}});
        return int(j["bufferViews"].size() - 1);
    };
    auto accessor = [&](const void* p, size_t n, int compType, size_t count, const char* type) {
        int v = view(p, n);
        j["accessors"].push_back({{"bufferView", v}, {"componentType", compType}, {"count", count}, {"type", type}});
        return int(j["accessors"].size() - 1);
    };
    size_t vc = pos.size() / 3;
    int aPos = accessor(pos.data(), pos.size() * 4, 5126, vc, "VEC3");
    float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i < vc; ++i)
        for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], pos[i * 3 + size_t(k)]); mx[k] = std::max(mx[k], pos[i * 3 + size_t(k)]); }
    j["accessors"][size_t(aPos)]["min"] = {mn[0], mn[1], mn[2]};
    j["accessors"][size_t(aPos)]["max"] = {mx[0], mx[1], mx[2]};
    int aNrm = accessor(nrm.data(), nrm.size() * 4, 5126, vc, "VEC3");
    int aUv = accessor(uv.data(), uv.size() * 4, 5126, vc, "VEC2");
    int aJ = accessor(joints.data(), joints.size() * 2, 5123, vc, "VEC4");
    int aW = accessor(weights.data(), weights.size() * 4, 5126, vc, "VEC4");
    int aI0 = accessor(idx.data(), opaqueIndexCount * 2, 5123, opaqueIndexCount, "SCALAR");
    int aI1 = accessor(idx.data() + opaqueIndexCount, (idx.size() - opaqueIndexCount) * 2, 5123, idx.size() - opaqueIndexCount, "SCALAR");
    std::vector<float> ibm;
    for (auto& n : nodes) {
        float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -n.p.x, -n.p.y, -n.p.z, 1};
        ibm.insert(ibm.end(), m, m + 16);
    }
    int aIbm = accessor(ibm.data(), ibm.size() * 4, 5126, nodes.size(), "MAT4");
    int imgView = view(png.data(), png.size());

    j["images"] = {{{"bufferView", imgView}, {"mimeType", "image/png"}}};
    j["textures"] = {{{"source", 0}}};
    j["materials"] = {{{"name", "body"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}},
                      {{"name", "cutout"}, {"alphaMode", "MASK"}, {"alphaCutoff", 0.5}, {"doubleSided", true},
                       {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}};
    json attrs = {{"POSITION", aPos}, {"NORMAL", aNrm}, {"TEXCOORD_0", aUv}, {"JOINTS_0", aJ}, {"WEIGHTS_0", aW}};
    j["meshes"] = {{{"primitives", {{{"attributes", attrs}, {"indices", aI0}, {"material", 0}},
                                    {{"attributes", attrs}, {"indices", aI1}, {"material", 1}}}}}};
    json jnodes = json::array();
    std::vector<int> jointList;
    for (size_t i = 0; i < nodes.size(); ++i) {
        Vec3 parentPos = nodes[i].parent < 0 ? Vec3{0, 0, 0} : nodes[size_t(nodes[i].parent)].p;
        Vec3 t = nodes[i].p - parentPos;
        json n = {{"name", nodes[i].human ? nodes[i].human : "J_Sec_Hair"}, {"translation", {t.x, t.y, t.z}}};
        json kids = json::array();
        for (size_t c = 0; c < nodes.size(); ++c)
            if (nodes[c].parent == int(i)) kids.push_back(c + 1);
        if (!kids.empty()) n["children"] = kids;
        jnodes.push_back(n);
        jointList.push_back(int(i + 1));
    }
    // node 0 = scene root holding hips and the mesh
    json root = {{"name", "Root"}, {"children", {1, int(nodes.size() + 1)}}};
    jnodes.insert(jnodes.begin(), root);
    jnodes.push_back({{"name", "Body"}, {"mesh", 0}, {"skin", 0}});
    j["nodes"] = jnodes;
    j["skins"] = {{{"joints", jointList}, {"inverseBindMatrices", aIbm}}};
    j["scenes"] = {{{"nodes", {0}}}};
    j["scene"] = 0;
    if (version == 0) {
        json hb = json::array();
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].human) hb.push_back({{"bone", nodes[i].human}, {"node", int(i + 1)}});
        j["extensionsUsed"] = {"VRM"};
        j["extensions"]["VRM"] = {{"specVersion", "0.0"}, {"meta", {{"title", "TestBoxPerson"}}}, {"humanoid", {{"humanBones", hb}}}};
    } else {
        json hb = json::object();
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].human) hb[nodes[i].human] = {{"node", int(i + 1)}};
        j["extensionsUsed"] = {"VRMC_vrm"};
        j["extensions"]["VRMC_vrm"] = {{"specVersion", "1.0"}, {"meta", {{"name", "TestBoxPerson"}}}, {"humanoid", {{"humanBones", hb}}}};
    }
    while (bin.size() % 4) bin.push_back(0);
    j["buffers"].push_back({{"byteLength", bin.size()}});
    std::string js = j.dump();
    while (js.size() % 4) js.push_back(' ');
    ByteWriter w;
    w.put<uint32_t>(0x46546C67);
    w.put<uint32_t>(2);
    w.put<uint32_t>(uint32_t(12 + 8 + js.size() + 8 + bin.size()));
    w.put<uint32_t>(uint32_t(js.size()));
    w.put<uint32_t>(0x4E4F534A);
    w.write(js.data(), js.size());
    w.put<uint32_t>(uint32_t(bin.size()));
    w.put<uint32_t>(0x004E4942);
    w.write(bin.data(), bin.size());
    writeFile(path, w.buf);
    return path;
}

// ---------------------------------------------------------------- synthetic game templates
static Mod3File templateModel(const HunterSkeleton& hs, const std::string& matName) {
    Mod3File f = hs.base;
    f.materials = {matName};
    Mod3Mesh m;
    m.h.blocktype = 0x81f58067; // 8 weight layout, like many real armour meshes
    m.h.shadowCast = 19;
    m.h.lod = 1;
    for (int k = 0; k < 4; ++k) {
        Mod3Vertex v;
        v.pos = hs.pos(pf::Chest) + Vec3{float(k & 1) * 10, float(k >> 1) * 10, 5};
        v.normal[3] = 0;
        v.tangent[3] = 127;
        v.bones[0] = uint8_t(hs.bone(pf::Chest));
        v.weights[0] = 1;
        m.vertices.push_back(v);
    }
    m.indices = {0, 1, 2, 2, 1, 3};
    m.boxes = computeMeshBoxes(m, hs.world);
    f.meshes.push_back(m);
    f.groups.resize(2);
    f.trailing = {1, 2, 3, 4};
    f.computeBounds();
    return f;
}

static Mrl3File templateMaterialFile(const std::string& matName, const std::string& texBase) {
    Mrl3File f;
    for (const char* suffix : {"_BML", "_NM", "_RMT", "_EM"}) {
        Mrl3Texture t;
        t.typeCode = jamcrc(std::string("rTexture")) & 0x7fffffff;
        t.path = texBase + suffix;
        f.textures.push_back(t);
    }
    Mrl3Material m;
    m.h.materialNameHash = jamcrc(matName);
    m.h.shaderHash = 0x12345678;
    // 5 bindings (80 bytes) + 12 floats
    m.h.floatArrayOffset = 10;
    m.data.resize(80 + 48, 0);
    auto bind = [&](int i, uint8_t type, const char* name, uint32_t tex) {
        Mrl3Binding b;
        b.resourceType = type;
        b.mapType = mapTypeId(name) << 12;
        b.texIdx = tex;
        m.setBinding(i, b);
    };
    bind(0, 0, "CBMhMaterialPLLocal__disclosure", 0);
    bind(1, 2, "tAlbedoMap__disclosure", 1);
    bind(2, 2, "tNormalMap__disclosure", 2);
    bind(3, 2, "tRMTMap__disclosure", 3);
    bind(4, 2, "tEmissiveMap__disclosure", 4);
    f.materials.push_back(m);
    return f;
}

static void writeGameFixture(const std::string& root) {
    HunterSkeleton hs = syntheticHunterSkeleton();
    for (char g : {'f', 'm'}) {
        for (const char* set : {"pl001_0000", "pl005_0000"}) {
            for (int s = 0; s < kSlotCount; ++s) {
                std::string slot = slotFolder(Slot(s));
                std::string dir = root + "/pl/" + std::string(1, g) + "_equip/" + set + "/" + slot + "/mod/";
                std::string base = std::string(1, g) + "_" + slot + std::string(set).substr(2);
                std::string mat = "PL_" + slot + "_Mt";
                writeFile(dir + base + ".mod3", templateModel(hs, mat).serialize());
                writeFile(dir + base + ".mrl3", templateMaterialFile(mat, "pl\\" + std::string(1, g) + "_equip\\" + set + "\\" + slot + "\\tex\\" + base).serialize());
            }
        }
        std::string face = root + "/pl/" + std::string(1, g) + "_face/face000/mod/face000";
        writeFile(face + ".mod3", templateModel(hs, "Face").serialize());
        writeFile(face + ".mrl3", templateMaterialFile("Face", "pl\\face").serialize());
    }
    writeFile(root + "/pl/hair/hair000/mod/hair000.mod3", templateModel(hs, "Hair").serialize());
    writeFile(root + "/pl/hair/hair000/mod/hair000.mrl3", templateMaterialFile("Hair", "pl\\hair").serialize());
}

// ---------------------------------------------------------------- tests

static void testMod3Roundtrip() {
    std::puts("mod3 round trip");
    HunterSkeleton hs = syntheticHunterSkeleton();
    Mod3File f = templateModel(hs, "abc");
    auto a = f.serialize();
    Mod3File g = Mod3File::parse(a);
    auto b = g.serialize();
    CHECK(a == b);
    CHECK(g.bones.size() == hs.base.bones.size());
    CHECK(g.meshes.size() == 1 && g.meshes[0].vertices.size() == 4 && g.meshes[0].indices.size() == 6);
    CHECK(g.materials.size() == 1 && g.materials[0] == "abc");
    CHECK(std::fabs(g.meshes[0].vertices[0].weights[0] - 1.0f) < 1e-3f);
    // world matrices survive
    Vec3 head = g.worldMatrix(g.boneByFunction(pf::Head)).translation();
    CHECK(length(head - hs.pos(pf::Head)) < 1e-3f);
    // A = inverse(world)
    Mat4 id = g.amats[size_t(g.boneByFunction(pf::HandL))] * g.worldMatrix(g.boneByFunction(pf::HandL));
    CHECK(std::fabs(id.at(0, 3)) < 1e-3f && std::fabs(id.at(0, 0) - 1) < 1e-4f);

    // three 30000-vertex meshes: the third one spills past 65535 into vertexBase
    Mod3File big = f;
    big.meshes.clear();
    for (int mi = 0; mi < 3; ++mi) {
        Mod3Mesh m;
        m.h.blocktype = 0xf637401c;
        for (int i = 0; i < 30000; ++i) {
            Mod3Vertex v;
            v.pos = {float(mi), float(i), 0};
            v.bones[0] = uint8_t(mi);
            v.weights[0] = 1;
            m.vertices.push_back(v);
        }
        for (int i = 0; i + 2 < 30000; i += 3) m.indices.insert(m.indices.end(), {uint16_t(i), uint16_t(i + 1), uint16_t(i + 2)});
        m.boxes = computeMeshBoxes(m, hs.world);
        big.meshes.push_back(m);
    }
    auto bigBytes = big.serialize();
    writeFile("/tmp/vh_test_big.mod3", bigBytes);
    Mod3File bigBack = Mod3File::parse(bigBytes);
    CHECK(bigBack.meshes.size() == 3);
    for (int mi = 0; mi < 3 && mi < int(bigBack.meshes.size()); ++mi) {
        const auto& m = bigBack.meshes[size_t(mi)];
        CHECK(m.vertices.size() == 30000);
        CHECK(m.vertices[12345].pos.x == float(mi) && m.vertices[12345].pos.y == 12345.0f);
        CHECK(m.indices[3] == 3);
    }
    CHECK(bigBack.meshes.size() == 3 && bigBack.meshes[2].h.vertexBase == 60000 && bigBack.meshes[2].h.vertexSub == 0);
}

static void testTex() {
    std::puts("tex writer + BC1");
    Image img;
    img.width = img.height = 16;
    img.rgba.resize(16 * 16 * 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            uint8_t* p = img.px(x, y);
            p[0] = uint8_t(x * 16); p[1] = uint8_t(y * 16); p[2] = 100; p[3] = (x < 2 && y < 2) ? 0 : 255;
        }
    auto t = buildTex(img, TEX_BC1_UNORM_SRGB, true);
    TexInfo info = parseTexHeader(t);
    CHECK(info.width == 16 && info.height == 16 && info.format == TEX_BC1_UNORM_SRGB);
    CHECK(info.mips == 3); // 16, 8, 4
    CHECK(info.mipOffsets.size() == 3 && info.mipOffsets[0] == 0xB8 + 3 * 8);
    CHECK(t.size() == info.mipOffsets[0] + 128 + 32 + 8);
    // decode the first block and compare
    uint8_t px[64];
    decodeBC1Block(&t[size_t(info.mipOffsets[0])], px);
    CHECK(px[3] == 0);           // transparent pixel kept transparent
    CHECK(px[(3 * 4 + 3) * 4 + 3] == 255);
    int maxErr = 0;
    for (int i = 0; i < 16; ++i) {
        if (px[i * 4 + 3] == 0) continue;
        const uint8_t* s = img.px(i % 4, i / 4);
        for (int c = 0; c < 3; ++c) maxErr = std::max(maxErr, std::abs(int(px[i * 4 + c]) - int(s[c])));
    }
    CHECK(maxErr < 40);
    auto raw = buildTex(Image::solid(4, 4, 1, 2, 3, 4), TEX_R8G8B8A8_UNORM, false);
    CHECK(raw.size() == 0xB8 + 8 + 64);
}

static void testMrl3Roundtrip() {
    std::puts("mrl3 round trip");
    Mrl3File f = templateMaterialFile("X", "a\\b");
    auto a = f.serialize();
    Mrl3File g = Mrl3File::parse(a);
    CHECK(g.serialize() == a);
    CHECK(g.textures.size() == 4 && g.textures[1].path == "a\\b_NM");
    CHECK(g.materials.size() == 1 && g.materials[0].bindingCount() == 5);
    CHECK(g.materials[0].findTextureBinding(mapTypeId("tAlbedoMap__disclosure")) == 1);
    CHECK(mapTypeId("tAlbedoMap") == 839791u); // value from the community shader map table
}

static void checkLimbsLandOnHunter(const VrmModel& vrm, const HunterSkeleton& hs, const RetargetResult& rr, const char* label) {
    (void)vrm;
    // every vertex skinned only to the left hand bone must be near the hunter's left hand
    int handL = hs.bone(pf::HandL), handR = hs.bone(pf::HandR), head = hs.bone(pf::Head);
    float worstL = 0, worstR = 0;
    int nL = 0, nR = 0;
    float maxY = -1e9f, minY = 1e9f;
    Vec3 up = normalize(hs.pos(pf::Head) - hs.pos(pf::Pelvis));
    for (auto& m : rr.meshes)
        for (auto& v : m.vertices) {
            maxY = std::max(maxY, dot(v.pos, up));
            minY = std::min(minY, dot(v.pos, up));
            if (v.weights[0] > 0.99f && v.bones[0] == handL) { worstL = std::max(worstL, length(v.pos - hs.pos(pf::HandL))); ++nL; }
            if (v.weights[0] > 0.99f && v.bones[0] == handR) { worstR = std::max(worstR, length(v.pos - hs.pos(pf::HandR))); ++nR; }
        }
    std::printf("  %s: scale %.1f, hand error L %.1f R %.1f, height span %.1f, mirrored %d\n", label, rr.report.scale, worstL, worstR,
                maxY - minY, rr.report.mirroredAxes ? 1 : 0);
    CHECK(nL > 0 && nR > 0);
    CHECK(worstL < 20.0f && worstR < 20.0f); // hand geometry around the hunter's hands (units: cm)
    (void)head;
}

static void testRetarget(const std::string& vrm0, const std::string& vrm1) {
    std::puts("retarget");
    HunterSkeleton hs = syntheticHunterSkeleton();
    for (const std::string& path : {vrm0, vrm1}) {
        VrmModel vrm = loadVrm(path);
        CHECK(vrm.human("leftHand") >= 0);
        RetargetResult rr = retargetToHunter(vrm, hs, RetargetOptions{});
        CHECK(!rr.meshes.empty());
        CHECK(rr.report.mappedBones >= 20);
        CHECK(!rr.report.mirroredAxes);
        CHECK(rr.report.droppedTriangles == 2); // the transparent quad
        // height: scale maps 1.37 m ankle-to-head onto the hunter
        CHECK(std::fabs(rr.report.vrmHeight - 1.37f) < 0.01f);
        checkLimbsLandOnHunter(vrm, hs, rr, path.c_str());
        // slots: hands in the arm slot, head in the helm slot
        bool armSlot = false, helmSlot = false;
        for (auto& m : rr.meshes) {
            if (m.slot == Slot::Arm) armSlot = true;
            if (m.slot == Slot::Helm) helmSlot = true;
            for (auto& v : m.vertices) {
                float s = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
                CHECK(std::fabs(s - 1) < 1e-3f);
            }
        }
        CHECK(armSlot && helmSlot);
    }

    // A mirrored (left handed) game skeleton must still put the VRM's left hand on the hunter's left hand.
    HunterSkeleton mirrored = hs;
    Mat4 mirror;
    mirror.at(0, 0) = -1;
    for (auto& w : mirrored.world) w = mirror * w * mirror; // keep bone frames right handed
    for (size_t i = 0; i < mirrored.base.lmats.size(); ++i) mirrored.base.lmats[i] = mirror * mirrored.base.lmats[i] * mirror;
    // swap L/R function ids to keep "left" on the character's left after mirroring
    VrmModel vrm = loadVrm(vrm1);
    RetargetResult rr = retargetToHunter(vrm, mirrored, RetargetOptions{});
    CHECK(rr.report.mirroredAxes);
    checkLimbsLandOnHunter(vrm, mirrored, rr, "mirrored skeleton");
    // in a mirrored space triangles must be flipped: check one box face normal against its winding
    int agree = 0, total = 0;
    for (auto& m : rr.meshes)
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            const OutVertex &a = m.vertices[m.indices[t]], &b = m.vertices[m.indices[t + 1]], &c = m.vertices[m.indices[t + 2]];
            Vec3 geo = cross(b.pos - a.pos, c.pos - a.pos);
            if (length(geo) < 1e-6f) continue;
            // retarget output always uses cross . normal > 0 for front faces
            agree += dot(geo, a.normal) > 0 ? 1 : 0;
            ++total;
        }
    std::printf("  mirrored winding agreement %d/%d\n", agree, total);
    CHECK(total > 0 && agree > total * 0.9);

    // matching a game that uses the opposite winding flips every triangle
    int ff = 0, bs = 0;
    measureConventions(rr.meshes, ff, bs);
    CHECK(ff == 1);
    matchGameConventions(rr.meshes, -1, bs);
    measureConventions(rr.meshes, ff, bs);
    CHECK(ff == -1);
}

static int linkCount(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return 0;
    return int(st.st_nlink);
}

static void testBuilder(const std::string& vrmPath) {
    std::puts("builder (synthetic game files)");
    std::string game = tmpDir("game_src");
    writeGameFixture(game);
    std::string native = tmpDir("nativePC");
    std::string data = native + "/plugins/MHWVroidHunter";
    makeDirs(data);
    // another mod already replaces one armour file: it must be left alone
    std::string foreign = native + "/pl/f_equip/pl005_0000/leg/mod/f_leg005_0000.mod3";
    writeFile(foreign, {9, 9, 9});

    ModManager mm(native, data, [&] { return openFolderSource(game); });
    Settings s;
    s.enabled = true;
    s.vrmPath = vrmPath;
    ApplyResult r = mm.apply(s);
    for (auto& l : r.log) std::printf("    %s\n", l.c_str());
    CHECK(r.ok);
    CHECK(r.skippedConflicts == 1);
    CHECK(readFile(foreign) == std::vector<uint8_t>({9, 9, 9}));
    // 2 genders x 2 sets x 5 slots x 2 files, minus the foreign file, + face (2x2) + hair (2)
    CHECK(r.linkedFiles == 2 * 2 * 5 * 2 - 1 + 4 + 2);

    std::string body = native + "/pl/f_equip/pl001_0000/body/mod/f_body001_0000.mod3";
    CHECK(exists(body));
    CHECK(linkCount(body) >= 3); // master + 2 armour sets
    Mod3File mod = Mod3File::parse(readFile(body));
    Mrl3File mrl = Mrl3File::parse(readFile(native + "/pl/f_equip/pl001_0000/body/mod/f_body001_0000.mrl3"));
    CHECK(!mod.meshes.empty());
    CHECK(mod.bones.size() == syntheticHunterSkeleton().base.bones.size());
    CHECK(mod.trailing == std::vector<uint8_t>({1, 2, 3, 4}));
    for (auto& name : mod.materials) {
        bool found = false;
        for (auto& m : mrl.materials) found = found || m.h.materialNameHash == jamcrc(name);
        CHECK(found);
    }
    for (auto& m : mrl.materials)
        for (int i = 0; i < m.bindingCount(); ++i) {
            Mrl3Binding b = m.binding(i);
            if ((b.resourceType & 0xF) != 2) continue;
            CHECK(b.texIdx >= 1 && b.texIdx <= mrl.textures.size());
            std::string texFile = native + "/" + mrl.textures[b.texIdx - 1].path + ".tex";
            for (char& c : texFile) if (c == '\\') c = '/';
            CHECK(exists(texFile));
            CHECK(mrl.textures[b.texIdx - 1].path.find("vroidhunter\\") == 0); // template textures never used
        }
    for (auto& m : mod.meshes) {
        CHECK(m.h.blocktype == 0xf637401c);
        CHECK(m.h.lod == 0xFFFF);
        std::map<int, bool> boxed;
        for (auto& b : m.boxes) boxed[b.boneIndex] = true;
        for (auto& v : m.vertices)
            for (int k = 0; k < 4; ++k) {
                CHECK(v.bones[k] < mod.bones.size());
                if (v.weights[k] > 0.01f) CHECK(boxed.count(v.bones[k]));
            }
    }
    CHECK(exists(native + "/pl/hair/hair000/mod/hair000.mod3"));

    // show the chest armour and the helmet: those links disappear, the rest stays
    s.showArmor[int(Slot::Body)] = true;
    s.showArmor[int(Slot::Helm)] = true;
    r = mm.apply(s);
    CHECK(r.ok);
    CHECK(!exists(body));
    CHECK(!exists(native + "/pl/hair/hair000/mod/hair000.mod3"));
    CHECK(exists(native + "/pl/f_equip/pl001_0000/arm/mod/f_arm001_0000.mod3"));
    CHECK(r.linkedFiles == 2 * 2 * 3 * 2 - 1);

    // disable: everything we made is gone, the foreign file remains
    s.enabled = false;
    r = mm.apply(s);
    CHECK(r.ok);
    int remaining = 0;
    for (auto& e : std::filesystem::recursive_directory_iterator(native))
        if (e.is_regular_file() && e.path().string().find("/plugins/") == std::string::npos) ++remaining;
    CHECK(remaining == 1);
    CHECK(exists(foreign));
}

static void testChunk() {
    std::puts("chunk archive (synthetic, encrypted, stored blocks)");
    // virtual stream: table at 0x100 with one directory and two files, data after it
    std::vector<uint8_t> stream(0x40000 + 5000, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&stream[at], &v, 4); };
    auto put64 = [&](size_t at, uint64_t v) { std::memcpy(&stream[at], &v, 8); };
    put32(0x0C, 1); // one parent
    put32(0x10, 3);
    size_t p = 0x100;
    std::memcpy(&stream[p], "\\", 1);
    put32(p + 0x3C + 16 + 4, 3); // children count
    p += 0x58;
    auto child = [&](const char* name, uint64_t size, uint64_t off, uint32_t type) {
        std::memcpy(&stream[p], name, std::strlen(name));
        put64(p + 0xA0, size);
        put64(p + 0xA8, off);
        put32(p + 0xB0, type);
        p += 0xB8;
    };
    child("\\pl\\f_equip", 0, 0, 1);
    const uint64_t fileOff = 0x40000 - 10; // crosses a block boundary
    child("\\pl\\f_equip\\pl001_0000\\body\\mod\\f_body001_0000.mod3", 20, fileOff, 0);
    child("\\pl\\f_equip\\pl001_0000\\body\\mod\\f_body001_0000.mrl3", 4, 100, 2);
    for (int i = 0; i < 20; ++i) stream[size_t(fileOff) + size_t(i)] = uint8_t(i + 1);
    std::memcpy(&stream[100], "MRL", 3);

    // encrypt blocks and write the archive with "stored" (size 0) entries
    const size_t blocks = 2;
    ByteWriter w;
    w.put<uint32_t>(0x00504B47);
    w.put<uint32_t>(uint32_t(blocks));
    size_t dataStart = 8 + blocks * 8;
    std::vector<std::vector<uint8_t>> enc;
    for (size_t b = 0; b < blocks; ++b) {
        std::vector<uint8_t> blk(stream.begin() + long(b * 0x40000), stream.begin() + long(std::min(stream.size(), (b + 1) * 0x40000)));
        decryptBlock(blk.data(), blk.size(), b);
        enc.push_back(blk);
    }
    size_t off = dataStart;
    for (size_t b = 0; b < blocks; ++b) {
        uint8_t e[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int k = 0; k < 5; ++k) e[3 + k] = uint8_t(off >> (8 * k));
        w.write(e, 8);
        off += enc[b].size();
    }
    for (auto& e : enc) w.write(e.data(), e.size());
    std::string dir = tmpDir("chunk");
    writeFile(dir + "/chunkG0.bin", w.buf);

    ChunkArchive arch(dir + "/chunkG0.bin", true, makeDecompressor(""));
    auto files = arch.listFiles();
    CHECK(files.size() == 2);
    if (files.size() == 2) {
        CHECK(files[0].path == "pl\\f_equip\\pl001_0000\\body\\mod\\f_body001_0000.mod3");
        auto bytes = arch.read(files[0].offset, files[0].size);
        CHECK(bytes.size() == 20 && bytes[0] == 1 && bytes[19] == 20);
        CHECK(files[1].path == "pl\\f_equip\\pl001_0000\\body\\mod\\f_body001_0000.mrl3");
    }
    GameArchives ga(dir);
    ga.index(isRelevantGamePath);
    CHECK(ga.files().size() == 2);
    CHECK(ga.read("PL/f_equip/pl001_0000/body/mod/f_body001_0000.mrl3")[0] == 'M');
    // key sequence sanity: the published order starts 0,1,2,3,4,5,6,0,7
    const uint8_t* k0 = chunkKeyForBlock(0);
    const uint8_t* k1 = chunkKeyForBlock(1);
    CHECK(k0[0] == 0xac && k1[0] == 0x6b && chunkKeyForBlock(2)[0] == 0x8f && chunkKeyForBlock(3)[0] == 0xda);
}

static void testDropFolder(const std::string& vrm0, const std::string& vrm1) {
    std::puts("drop folder + converter job");
    std::string game = tmpDir("dropgame");
    std::string src = tmpDir("drop_src");
    writeGameFixture(src);
    auto factory = [&] { return openFolderSource(src); };
    // nothing in the folder yet
    JobResult r = runJob(game, JobMode::Auto, nullptr, factory);
    CHECK(r.ok && !r.didWork);
    CHECK(exists(modelsFolder(game) + "/PUT YOUR .VRM FILE HERE.txt"));
    // drop a model: picked up automatically
    std::filesystem::copy_file(vrm0, modelsFolder(game) + "/first.vrm");
    r = runJob(game, JobMode::Auto, nullptr, factory);
    CHECK(r.ok && r.didWork);
    const std::string body = game + "/nativePC/pl/f_equip/pl001_0000/body/mod/f_body001_0000.mod3";
    CHECK(exists(body));
    // second start without changes: no work
    r = runJob(game, JobMode::Auto, nullptr, factory);
    CHECK(r.ok && !r.didWork);
    // a newer file wins
    std::filesystem::copy_file(vrm1, modelsFolder(game) + "/second.vrm");
    std::filesystem::last_write_time(modelsFolder(game) + "/second.vrm",
                                     std::filesystem::last_write_time(modelsFolder(game) + "/first.vrm") + std::chrono::seconds(10));
    Settings s = Settings::load(pluginDataDir(game) + "/settings.ini");
    CHECK(resolveModel(game, s).find("second.vrm") != std::string::npos);
    r = runJob(game, JobMode::Auto, nullptr, factory);
    CHECK(r.ok && r.didWork);
    // picking the older one in the menu sticks until a newer file is dropped
    pickModel(game, s, "first.vrm");
    s.save(pluginDataDir(game) + "/settings.ini");
    CHECK(resolveModel(game, s).find("first.vrm") != std::string::npos);
    r = runJob(game, JobMode::Auto, nullptr, factory);
    CHECK(r.ok && r.didWork);
    // turning the mod off removes the files
    s.enabled = false;
    s.save(pluginDataDir(game) + "/settings.ini");
    r = runJob(game, JobMode::Auto, nullptr, factory);
    CHECK(r.ok && !exists(body));
    CHECK(exists(modelsFolder(game) + "/first.vrm")); // user files are never touched
    r = runJob(game, JobMode::Remove, nullptr, factory);
    CHECK(r.ok);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    try {
        std::string dir = tmpDir("vrm");
        std::string v0 = writeTestVrm(dir + "/box0.vrm", 0);
        std::string v1 = writeTestVrm(dir + "/box1.vrm", 1);
        testMod3Roundtrip();
        testTex();
        testMrl3Roundtrip();
        testRetarget(v0, v1);
        testChunk();
        testBuilder(v1);
        testDropFolder(v0, v1);
        if (const char* real = std::getenv("VH_TEST_VRM")) {
            std::printf("builder with %s\n", real);
            testBuilder(real);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 2;
    }
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("all tests passed");
    return 0;
}
