// MT Framework .mrl3 material libraries (Iceborne layout).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vh {

#pragma pack(push, 1)
struct Mrl3MaterialHeader {
    uint32_t headId = 0x4516E7AB;   // nDraw::Material
    uint32_t materialNameHash = 0;  // JAMCRC of the material name used in the mod3
    uint32_t shaderHash = 0;
    uint32_t skinId = 0;
    uint32_t matSize = 0;           // size of the data block (resource bindings + parameters)
    int16_t unkn4 = 0;
    uint8_t floatArrayOffset = 0;   // resource binding area size in units of 8 bytes
    uint8_t unkn5[9] = {};
    uint8_t unkn6 = 0;
    uint8_t unkn7[15] = {};
    uint32_t startAddress = 0;      // absolute file offset of the data block
    int32_t unkn8 = 0;
};
static_assert(sizeof(Mrl3MaterialHeader) == 56, "mrl3 material header layout");

struct Mrl3Binding {
    uint8_t resourceType = 0; // low nibble: 0 cbuffer, 1 sampler, 2 texture
    uint8_t unknArr[3] = {};
    uint32_t mapType = 0;     // upper 20 bits: JAMCRC(name) & 0xFFFFF
    uint32_t texIdx = 0;      // 1-based index into the texture list (0 = none)
    uint32_t unkn = 0;
};
static_assert(sizeof(Mrl3Binding) == 16, "mrl3 binding layout");
#pragma pack(pop)

struct Mrl3Texture {
    uint32_t typeCode = 0;
    std::array<uint8_t, 12> unkn{};
    std::string path; // resource path without extension, backslash separated
};

struct Mrl3Material {
    Mrl3MaterialHeader h;
    std::vector<uint8_t> data; // matSize bytes: bindings followed by the parameter array

    int bindingCount() const { return h.floatArrayOffset * 8 / 16; }
    Mrl3Binding binding(int i) const;
    void setBinding(int i, const Mrl3Binding& b);
    // Index of the first texture binding whose map type name matches, or -1.
    int findTextureBinding(uint32_t mapTypeId) const;
};

struct Mrl3File {
    uint32_t magic = 0x004C524D; // "MRL\0"
    std::array<uint8_t, 12> unkn{};
    std::vector<Mrl3Texture> textures;
    std::vector<Mrl3Material> materials;

    static Mrl3File parse(const std::vector<uint8_t>& data);
    std::vector<uint8_t> serialize() const;
};

// 20-bit map type id for a shader resource name, e.g. "tAlbedoMap".
uint32_t mapTypeId(const std::string& name);
uint32_t bindingMapTypeId(const Mrl3Binding& b);

} // namespace vh
