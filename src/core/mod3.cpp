#include "mod3.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "binio.h"

namespace vh {

namespace {

const VertexLayout kLayouts[] = {
    {0xa756f2f9, "IANonSkin1UV", 1, 0, false},
    {0xa5104ca0, "IANonSkin2UV", 2, 0, false},
    {0xc3ac03a1, "IANonSkin3UVColor", 3, 0, true},
    {0xc9690ab8, "IANonSkin4UVColor", 4, 0, true},
    {0x818904dc, "IANonSkin1UVColor", 1, 0, true},
    {0x0f06033f, "IANonSkin2UVColor", 2, 0, true},
    {0xf637401c, "IASkin4wt1UV", 1, 4, false},
    {0xf471fe45, "IASkin4wt2UV", 2, 4, false},
    {0x3c730760, "IASkin4wt1UVColor", 1, 4, true},
    {0xb2fc0083, "IASkin4wt2UVColor", 2, 4, true},
    {0x81f58067, "IASkin8wt1UV", 1, 8, false},
    {0x83b33e3e, "IASkin8wt2UV", 2, 8, false},
    {0x366995a7, "IASkin8wt1UVColor", 1, 8, true},
    {0xb8e69244, "IASkin8wt2UVColor", 2, 8, true},
};

constexpr size_t kHeaderSize = 112;
constexpr size_t kHeaderTotal = kHeaderSize + 48 + 96 + 64; // 0x140

uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = int32_t((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x7fffff;
    if (((x >> 23) & 0xff) == 0xff) return uint16_t(sign | 0x7c00 | (mant ? 0x200 : 0));
    if (exp >= 31) return uint16_t(sign | 0x7bff); // clamp to max finite
    if (exp <= 0) {
        if (exp < -10) return uint16_t(sign);
        mant |= 0x800000;
        uint32_t shift = uint32_t(14 - exp);
        uint32_t half = mant >> shift;
        if ((mant >> (shift - 1)) & 1) half += 1; // round
        return uint16_t(sign | half);
    }
    uint32_t half = sign | (uint32_t(exp) << 10) | (mant >> 13);
    if (mant & 0x1000) half += 1; // round to nearest
    return uint16_t(half);
}

float halfToFloat(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t out;
    if (exp == 0) {
        if (mant == 0) {
            out = sign;
        } else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) { mant <<= 1; --exp; }
            mant &= 0x3ff;
            out = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out = sign | 0x7f800000 | (mant << 13);
    } else {
        out = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &out, 4);
    return f;
}

Mat4 readMatrix(ByteReader& r) {
    Mat4 m;
    for (int i = 0; i < 16; ++i) m.m[i] = r.get<float>();
    return m;
}

void writeMatrix(ByteWriter& w, const Mat4& m) {
    for (int i = 0; i < 16; ++i) w.put(m.m[i]);
}

// Packs up to 3 weights into 3x10 bits (the 4th weight is the complement).
uint32_t pack10(const float* w, int n, float scale = 1023.0f) {
    uint32_t q[3] = {0, 0, 0};
    int total = 0;
    for (int i = 0; i < 3 && i < n; ++i) {
        int v = int(std::lround(clamp01(w[i]) * scale));
        v = std::min(v, 1023 - total);
        v = std::max(v, 0);
        q[i] = uint32_t(v);
        total += v;
    }
    return q[0] | (q[1] << 10) | (q[2] << 20);
}

void readVertex(ByteReader& r, const VertexLayout& L, Mod3Vertex& v) {
    v.pos.x = r.get<float>();
    v.pos.y = r.get<float>();
    v.pos.z = r.get<float>();
    r.read(v.normal, 4);
    r.read(v.tangent, 4);
    for (int i = 0; i < L.uvs; ++i) {
        v.uv[i][0] = halfToFloat(r.get<uint16_t>());
        v.uv[i][1] = halfToFloat(r.get<uint16_t>());
    }
    if (L.weights == 4) {
        uint32_t packed = r.get<uint32_t>();
        float sum = 0;
        for (int i = 0; i < 3; ++i) {
            v.weights[i] = float((packed >> (10 * i)) & 0x3ff) / 1023.0f;
            sum += v.weights[i];
        }
        v.weights[3] = 1.0f - sum;
        r.read(v.bones, 4);
    } else if (L.weights == 8) {
        uint32_t packed = r.get<uint32_t>();
        uint8_t extra[4];
        r.read(extra, 4);
        float sum = 0;
        for (int i = 0; i < 3; ++i) {
            v.weights[i] = float((packed >> (10 * i)) & 0x3ff) / 1023.0f;
            sum += v.weights[i];
        }
        for (int i = 0; i < 4; ++i) {
            v.weights[3 + i] = extra[i] / 255.0f;
            sum += v.weights[3 + i];
        }
        v.weights[7] = 1.0f - sum;
        r.read(v.bones, 8);
    }
    if (L.colour) r.read(v.colour, 4);
}

void writeVertex(ByteWriter& w, const VertexLayout& L, const Mod3Vertex& v) {
    w.put(v.pos.x);
    w.put(v.pos.y);
    w.put(v.pos.z);
    w.write(v.normal, 4);
    w.write(v.tangent, 4);
    for (int i = 0; i < L.uvs; ++i) {
        w.put(floatToHalf(v.uv[i][0]));
        w.put(floatToHalf(v.uv[i][1]));
    }
    if (L.weights == 4) {
        w.put(pack10(v.weights, 3));
        w.write(v.bones, 4);
    } else if (L.weights == 8) {
        uint32_t p = pack10(v.weights, 3);
        w.put(p);
        int used = int(p & 0x3ff) + int((p >> 10) & 0x3ff) + int((p >> 20) & 0x3ff);
        float remaining = 1.0f - used / 1023.0f;
        uint8_t extra[4];
        for (int i = 0; i < 4; ++i) {
            float x = std::min(clamp01(v.weights[3 + i]), std::max(remaining, 0.0f));
            int q = int(std::lround(x * 255.0f));
            extra[i] = uint8_t(std::clamp(q, 0, 255));
            remaining -= extra[i] / 255.0f;
        }
        w.write(extra, 4);
        w.write(v.bones, 8);
    }
    if (L.colour) w.write(v.colour, 4);
}

} // namespace

const VertexLayout* findLayout(uint32_t blocktype) {
    for (const auto& l : kLayouts)
        if (l.blocktype == blocktype) return &l;
    return nullptr;
}

const VertexLayout* findLayout(int uvs, int weights, bool colour) {
    for (const auto& l : kLayouts)
        if (l.uvs == uvs && l.weights == weights && l.colour == colour) return &l;
    return nullptr;
}

Mod3File Mod3File::parse(const std::vector<uint8_t>& data) {
    ByteReader r(data);
    Mod3File f;
    uint32_t magic = r.get<uint32_t>();
    if (magic != 0x444F4D) throw FormatError("not a mod3 file");
    f.version = r.get<uint8_t>();
    f.version2 = r.get<uint8_t>();
    uint16_t boneCount = r.get<uint16_t>();
    uint16_t meshCount = r.get<uint16_t>();
    uint16_t materialCount = r.get<uint16_t>();
    r.get<uint32_t>(); // vertexCount
    r.get<uint32_t>(); // faceCount
    f.vertexIds = r.get<uint32_t>();
    r.get<uint32_t>(); // vertexBufferSize
    f.secondBufferSize = r.get<uint32_t>();
    uint64_t groupCount = r.get<uint64_t>();
    f.creationDate = r.get<uint64_t>();
    uint64_t boneOffset = r.get<uint64_t>();
    uint64_t groupOffset = r.get<uint64_t>();
    uint64_t materialOffset = r.get<uint64_t>();
    uint64_t meshOffset = r.get<uint64_t>();
    uint64_t vertexOffset = r.get<uint64_t>();
    uint64_t facesOffset = r.get<uint64_t>();
    uint64_t trailOffset = r.get<uint64_t>();
    f.unkOffset2 = r.get<uint64_t>();
    r.read(f.boundCenter, 12);
    f.boundRadius = r.get<float>();
    r.read(f.boundMin, 16);
    r.read(f.boundMax, 16);
    r.read(f.floatSegment.data(), 96);
    r.read(f.byteSegment.data(), 64);

    f.boneMap.fill(255);
    if (boneCount) {
        r.seek(boneOffset);
        f.bones.resize(boneCount);
        r.read(f.bones.data(), boneCount * sizeof(Mod3Bone));
        for (int i = 0; i < boneCount; ++i) f.lmats.push_back(readMatrix(r));
        for (int i = 0; i < boneCount; ++i) f.amats.push_back(readMatrix(r));
        r.read(f.boneMap.data(), 512);
    }

    if (groupCount > 100000) throw FormatError("implausible group count");
    r.seek(groupOffset);
    f.groups.resize(size_t(groupCount));
    for (auto& g : f.groups) r.read(g.data(), 32);

    r.seek(materialOffset);
    for (int i = 0; i < materialCount; ++i) f.materials.push_back(r.fixedString(128));

    r.seek(meshOffset);
    f.meshes.resize(meshCount);
    for (auto& m : f.meshes) r.read(&m.h, sizeof(Mod3MeshHeader));
    uint32_t boxCount = r.get<uint32_t>();
    std::vector<Mod3BoundingBox> boxes(boxCount);
    if (boxCount) r.read(boxes.data(), boxCount * sizeof(Mod3BoundingBox));
    size_t boxIt = 0;
    for (auto& m : f.meshes) {
        for (int i = 0; i < m.h.boundingBoxCount && boxIt < boxes.size(); ++i) m.boxes.push_back(boxes[boxIt++]);
    }

    for (auto& m : f.meshes) {
        const VertexLayout* L = findLayout(m.h.blocktype);
        if (!L) throw FormatError("unknown vertex blocktype");
        if (L->blockSize() != m.h.blockSize) throw FormatError("blocksize mismatch");
        // faces
        r.seek(facesOffset + uint64_t(m.h.faceOffset) * 2);
        std::vector<uint32_t> raw(m.h.faceCount);
        uint32_t vmin = 0xFFFFFFFF, vmax = 0;
        for (auto& i : raw) {
            i = r.get<uint16_t>();
            vmin = std::min(vmin, i);
            vmax = std::max(vmax, i);
        }
        uint32_t adj = 0;
        if (!raw.empty() && vmin < m.h.vertexSub) adj = m.h.vertexSub - vmin;
        uint32_t count = m.h.vertexCount;
        if (!raw.empty()) count = std::max<uint32_t>(count, vmax - std::min<uint32_t>(vmin, m.h.vertexSub) + 1);
        m.indices.resize(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) m.indices[i] = uint16_t(raw[i] + adj - m.h.vertexSub);
        r.seek(vertexOffset + m.h.vertexOffset +
               uint64_t(m.h.blockSize) * (uint64_t(m.h.vertexSub) + m.h.vertexBase - adj));
        m.vertices.resize(count);
        for (auto& v : m.vertices) readVertex(r, *L, v);
    }

    if (trailOffset && trailOffset <= data.size()) f.trailing.assign(data.begin() + long(trailOffset), data.end());
    return f;
}

std::vector<uint8_t> Mod3File::serialize() const {
    // Work on copies of the mesh headers so offsets can be computed.
    std::vector<Mod3MeshHeader> hs;
    size_t totalBoxes = 0;
    for (const auto& m : meshes) {
        const VertexLayout* L = findLayout(m.h.blocktype);
        if (!L) throw FormatError("unknown vertex blocktype on write");
        if (m.vertices.size() > 0xFFFF) throw FormatError("mesh has more than 65535 vertices");
        if (m.boxes.size() > 255) throw FormatError("too many bounding boxes in one mesh");
        Mod3MeshHeader h = m.h;
        h.blockSize = uint8_t(L->blockSize());
        h.vertexCount = uint16_t(m.vertices.size());
        h.faceCount = uint32_t(m.indices.size());
        h.boundingBoxCount = uint8_t(m.boxes.size());
        totalBoxes += m.boxes.size();
        hs.push_back(h);
    }

    // Vertex buffer bookkeeping (see "Vertex Base, Sub and Offset" on the MHW modding wiki).
    uint32_t vCount = 0, vBufferLen = 0, prevBase = 0, prevSub = 0, prevBlock = 0;
    for (auto& h : hs) {
        if (h.blockSize != prevBlock) {
            h.vertexOffset = vBufferLen;
            h.vertexSub = 0;
            h.vertexBase = 0;
        } else {
            if (uint32_t(h.vertexCount) + prevSub > 0xFFFF) {
                h.vertexSub = 0;
                h.vertexBase = prevBase + prevSub;
            } else {
                h.vertexSub = prevSub;
                h.vertexBase = prevBase;
            }
            h.vertexOffset = vBufferLen - (h.vertexBase + h.vertexSub) * h.blockSize;
        }
        h.vertexSubMirror = uint16_t(h.vertexSub);
        h.vertexSubTotal = vCount;
        h.vertexIndexSub = uint16_t(h.vertexSub + h.vertexCount - 1);
        vCount += h.vertexCount;
        vBufferLen += uint32_t(h.vertexCount) * h.blockSize;
        prevBase = h.vertexBase;
        prevSub = h.vertexSub + h.vertexCount;
        prevBlock = h.blockSize;
    }
    uint32_t faceCursor = 0, totalIndices = 0;
    for (size_t i = 0; i < hs.size(); ++i) {
        hs[i].faceOffset = faceCursor;
        faceCursor += uint32_t(meshes[i].indices.size());
        totalIndices += uint32_t(meshes[i].indices.size());
    }

    size_t skeletonLen = bones.empty() ? 0 : bones.size() * (sizeof(Mod3Bone) + 128) + 512;
    uint64_t boneOffset = bones.empty() ? 0 : alignUp(kHeaderTotal, 16);
    uint64_t groupOffset = alignUp((bones.empty() ? kHeaderTotal : boneOffset) + skeletonLen, 16);
    uint64_t materialOffset = alignUp(groupOffset + groups.size() * 32, 16);
    uint64_t meshOffset = alignUp(materialOffset + materials.size() * 128, 16);
    uint64_t vertexOffset = meshOffset + hs.size() * sizeof(Mod3MeshHeader) + 4 + totalBoxes * sizeof(Mod3BoundingBox);
    uint64_t facesOffset = vertexOffset + vBufferLen;
    uint64_t trailOffset = alignUp(facesOffset + uint64_t(totalIndices) * 2, 4);

    ByteWriter w;
    w.put<uint32_t>(0x444F4D);
    w.put(version);
    w.put(version2);
    w.put<uint16_t>(uint16_t(bones.size()));
    w.put<uint16_t>(uint16_t(meshes.size()));
    w.put<uint16_t>(uint16_t(materials.size()));
    w.put<uint32_t>(vCount);
    w.put<uint32_t>(totalIndices);
    w.put<uint32_t>(vertexIds);
    w.put<uint32_t>(vBufferLen);
    w.put<uint32_t>(secondBufferSize);
    w.put<uint64_t>(groups.size());
    w.put<uint64_t>(creationDate);
    w.put<uint64_t>(boneOffset);
    w.put<uint64_t>(groupOffset);
    w.put<uint64_t>(materialOffset);
    w.put<uint64_t>(meshOffset);
    w.put<uint64_t>(vertexOffset);
    w.put<uint64_t>(facesOffset);
    w.put<uint64_t>(trailOffset);
    w.put<uint64_t>(unkOffset2);
    w.write(boundCenter, 12);
    w.put(boundRadius);
    w.write(boundMin, 16);
    w.write(boundMax, 16);
    w.write(floatSegment.data(), 96);
    w.write(byteSegment.data(), 64);

    if (!bones.empty()) {
        if (lmats.size() != bones.size() || amats.size() != bones.size()) throw FormatError("matrix count mismatch");
        w.padTo(boneOffset);
        w.write(bones.data(), bones.size() * sizeof(Mod3Bone));
        for (const auto& m : lmats) writeMatrix(w, m);
        for (const auto& m : amats) writeMatrix(w, m);
        w.write(boneMap.data(), 512);
    }
    w.padTo(groupOffset);
    for (const auto& g : groups) w.write(g.data(), 32);
    w.padTo(materialOffset);
    for (const auto& m : materials) w.fixedString(m, 128);
    w.padTo(meshOffset);
    for (const auto& h : hs) w.put(h);
    w.put<uint32_t>(uint32_t(totalBoxes));
    for (const auto& m : meshes)
        for (const auto& b : m.boxes) w.put(b);
    if (w.tell() != vertexOffset) throw FormatError("internal: vertex offset mismatch");
    for (size_t i = 0; i < meshes.size(); ++i) {
        const VertexLayout* L = findLayout(hs[i].blocktype);
        for (const auto& v : meshes[i].vertices) writeVertex(w, *L, v);
    }
    if (w.tell() != facesOffset) throw FormatError("internal: face offset mismatch");
    for (size_t i = 0; i < meshes.size(); ++i) {
        for (uint16_t idx : meshes[i].indices) {
            uint32_t g = uint32_t(idx) + hs[i].vertexSub;
            if (g > 0xFFFF) throw FormatError("face index overflow");
            w.put<uint16_t>(uint16_t(g));
        }
    }
    w.padTo(trailOffset);
    w.write(trailing.data(), trailing.size());
    return w.buf;
}

void Mod3File::rebuildBoneMap() {
    boneMap.fill(255);
    for (size_t i = 0; i < bones.size(); ++i) {
        int f = bones[i].function;
        if (f >= 0 && f < 512) boneMap[size_t(f)] = uint8_t(i);
    }
}

void Mod3File::computeBounds() {
    Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (const auto& m : meshes)
        for (const auto& v : m.vertices) {
            any = true;
            for (int k = 0; k < 3; ++k) {
                mn[k] = std::min(mn[k], v.pos[k]);
                mx[k] = std::max(mx[k], v.pos[k]);
            }
        }
    if (!any) mn = mx = Vec3{0, 0, 0};
    Vec3 c = (mn + mx) * 0.5f;
    boundCenter[0] = c.x; boundCenter[1] = c.y; boundCenter[2] = c.z;
    boundRadius = length(mx - mn) * 0.5f;
    boundMin[0] = mn.x; boundMin[1] = mn.y; boundMin[2] = mn.z; boundMin[3] = 0;
    boundMax[0] = mx.x; boundMax[1] = mx.y; boundMax[2] = mx.z; boundMax[3] = 0;
}

Mat4 Mod3File::worldMatrix(int bone) const {
    Mat4 m = lmats.at(size_t(bone));
    int p = bones.at(size_t(bone)).parent;
    int guard = 0;
    while (p != 255 && p < int(bones.size()) && guard++ < 512) {
        m = lmats[size_t(p)] * m;
        p = bones[size_t(p)].parent;
    }
    return m;
}

int Mod3File::boneByFunction(int function) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].function == function) return int(i);
    return -1;
}

std::vector<Mod3BoundingBox> computeMeshBoxes(const Mod3Mesh& mesh, const std::vector<Mat4>& world) {
    const VertexLayout* L = findLayout(mesh.h.blocktype);
    int nw = L ? L->weights : 0;
    std::map<int, std::pair<Vec3, Vec3>> ranges;
    auto add = [&](int bone, const Vec3& p) {
        auto it = ranges.find(bone);
        if (it == ranges.end()) {
            ranges[bone] = {p, p};
        } else {
            for (int k = 0; k < 3; ++k) {
                it->second.first[k] = std::min(it->second.first[k], p[k]);
                it->second.second[k] = std::max(it->second.second[k], p[k]);
            }
        }
    };
    std::vector<Mat4> inv;
    inv.reserve(world.size());
    for (const auto& m : world) inv.push_back(m.inverse());
    for (const auto& v : mesh.vertices) {
        if (nw == 0) {
            add(255, v.pos);
            continue;
        }
        for (int i = 0; i < nw; ++i) {
            if (v.weights[i] <= 0.0f) continue;
            int b = v.bones[i];
            if (b < int(inv.size())) add(b, inv[size_t(b)].transformPoint(v.pos));
        }
    }
    std::vector<Mod3BoundingBox> out;
    for (const auto& kv : ranges) {
        Mod3BoundingBox box;
        box.boneIndex = kv.first;
        Vec3 mn = kv.second.first, mx = kv.second.second;
        Vec3 c = (mn + mx) * 0.5f, half = (mx - mn) * 0.5f;
        // keep boxes non-degenerate so they are never culled away
        for (int k = 0; k < 3; ++k) half[k] = std::max(half[k], 0.5f);
        box.center[0] = c.x; box.center[1] = c.y; box.center[2] = c.z;
        box.radius = length(half);
        box.aabbMin[0] = c.x - half.x; box.aabbMin[1] = c.y - half.y; box.aabbMin[2] = c.z - half.z;
        box.aabbMax[0] = c.x + half.x; box.aabbMax[1] = c.y + half.y; box.aabbMax[2] = c.z + half.z;
        // oriented box = axis aligned in bone space: identity rotation, translation = centre (column-major)
        float om[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, c.x, c.y, c.z, 1};
        std::memcpy(box.oabbMatrix, om, sizeof(om));
        box.oabbVector[0] = half.x; box.oabbVector[1] = half.y; box.oabbVector[2] = half.z; box.oabbVector[3] = 0;
        out.push_back(box);
    }
    std::sort(out.begin(), out.end(), [](const Mod3BoundingBox& a, const Mod3BoundingBox& b) {
        int ka = a.boneIndex == 255 ? -1 : a.boneIndex, kb = b.boneIndex == 255 ? -1 : b.boneIndex;
        return ka < kb;
    });
    return out;
}

uint32_t jamcrc(const void* data, size_t len) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc; // JAMCRC = CRC32 without the final inversion
}

} // namespace vh
