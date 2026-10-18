// Monster Hunter World .tex textures (DDS payload behind a 0xB8 byte header).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vh {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // 8-bit RGBA, row-major, top row first

    uint8_t* px(int x, int y) { return &rgba[(size_t(y) * width + x) * 4]; }
    const uint8_t* px(int x, int y) const { return &rgba[(size_t(y) * width + x) * 4]; }
    static Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    // Bilinear sample with wrap addressing, uv in [0,1] (glTF convention, v down).
    void sample(float u, float v, float out[4]) const;
};

enum TexFormat : uint32_t {
    TEX_R8G8B8A8_UNORM = 7,
    TEX_R8G8B8A8_UNORM_SRGB = 9,
    TEX_BC1_UNORM = 22,
    TEX_BC1_UNORM_SRGB = 23,
};

// Resizes to the nearest power of two no larger than maxSize (and at least 4).
Image fitToPowerOfTwo(const Image& src, int maxSize);
// Half-size box filter; srgb=true filters colour channels in linear space.
Image downsample(const Image& src, bool srgb);

// Encodes one 4x4 RGBA block into 8 bytes of BC1. Pixels with alpha < 128 use the
// transparent (punch-through) index.
void encodeBC1Block(const uint8_t rgba[16 * 4], uint8_t out[8]);
void decodeBC1Block(const uint8_t in[8], uint8_t rgba[16 * 4]);

// Builds a complete .tex file with a full mip chain (down to 4x4).
std::vector<uint8_t> buildTex(const Image& img, TexFormat format, bool srgbMips);

struct TexInfo {
    uint32_t format = 0;
    int width = 0, height = 0, mips = 0;
    std::vector<uint64_t> mipOffsets;
};
TexInfo parseTexHeader(const std::vector<uint8_t>& data);

} // namespace vh
