#include "mrl3.h"

#include <cstring>

#include "binio.h"

namespace vh {

uint32_t mapTypeId(const std::string& name) { return jamcrc(name) & 0xFFFFF; }
uint32_t bindingMapTypeId(const Mrl3Binding& b) { return b.mapType >> 12; }

Mrl3Binding Mrl3Material::binding(int i) const {
    Mrl3Binding b;
    std::memcpy(&b, data.data() + size_t(i) * 16, 16);
    return b;
}

void Mrl3Material::setBinding(int i, const Mrl3Binding& b) { std::memcpy(data.data() + size_t(i) * 16, &b, 16); }

int Mrl3Material::findTextureBinding(uint32_t id) const {
    for (int i = 0; i < bindingCount(); ++i) {
        Mrl3Binding b = binding(i);
        if ((b.resourceType & 0xF) == 2 && bindingMapTypeId(b) == id) return i;
    }
    return -1;
}

Mrl3File Mrl3File::parse(const std::vector<uint8_t>& bytes) {
    ByteReader r(bytes);
    Mrl3File f;
    f.magic = r.get<uint32_t>();
    if ((f.magic & 0x00FFFFFF) != 0x004C524D) throw FormatError("not an mrl3 file");
    r.read(f.unkn.data(), 12);
    uint32_t materialCount = r.get<uint32_t>();
    uint32_t textureCount = r.get<uint32_t>();
    uint64_t textureOffset = r.get<uint64_t>();
    uint64_t materialOffset = r.get<uint64_t>();
    if (materialCount > 4096 || textureCount > 4096) throw FormatError("implausible mrl3 counts");
    r.seek(textureOffset);
    for (uint32_t i = 0; i < textureCount; ++i) {
        Mrl3Texture t;
        t.typeCode = r.get<uint32_t>();
        r.read(t.unkn.data(), 12);
        t.path = r.fixedString(256);
        f.textures.push_back(t);
    }
    r.seek(materialOffset);
    for (uint32_t i = 0; i < materialCount; ++i) {
        Mrl3Material m;
        r.read(&m.h, sizeof(m.h));
        f.materials.push_back(m);
    }
    for (auto& m : f.materials) {
        r.seek(m.h.startAddress);
        m.data = r.bytes(m.h.matSize);
        if (size_t(m.h.floatArrayOffset) * 8 > m.data.size()) throw FormatError("bad mrl3 binding size");
    }
    return f;
}

std::vector<uint8_t> Mrl3File::serialize() const {
    ByteWriter w;
    const uint64_t textureOffset = 0x28;
    const uint64_t materialOffset = textureOffset + textures.size() * 0x110;
    w.put(magic);
    w.write(unkn.data(), 12);
    w.put<uint32_t>(uint32_t(materials.size()));
    w.put<uint32_t>(uint32_t(textures.size()));
    w.put<uint64_t>(textureOffset);
    w.put<uint64_t>(materialOffset);
    for (const auto& t : textures) {
        w.put(t.typeCode);
        w.write(t.unkn.data(), 12);
        w.fixedString(t.path, 256);
    }
    // material headers, then the data blocks (16-byte aligned)
    size_t cursor = alignUp(size_t(materialOffset + materials.size() * sizeof(Mrl3MaterialHeader)), 16);
    std::vector<uint32_t> starts;
    for (const auto& m : materials) {
        starts.push_back(uint32_t(cursor));
        cursor = alignUp(cursor + m.data.size(), 16);
    }
    for (size_t i = 0; i < materials.size(); ++i) {
        Mrl3MaterialHeader h = materials[i].h;
        h.matSize = uint32_t(materials[i].data.size());
        h.startAddress = starts[i];
        w.put(h);
    }
    for (size_t i = 0; i < materials.size(); ++i) {
        w.padTo(starts[i]);
        w.write(materials[i].data.data(), materials[i].data.size());
    }
    w.align(16);
    return w.buf;
}

} // namespace vh
