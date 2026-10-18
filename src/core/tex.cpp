#include "tex.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "binio.h"

namespace vh {

namespace {

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

uint16_t to565(const float c[3]) {
    int r = std::clamp(int(std::lround(c[0] * 31.0f / 255.0f)), 0, 31);
    int g = std::clamp(int(std::lround(c[1] * 63.0f / 255.0f)), 0, 63);
    int b = std::clamp(int(std::lround(c[2] * 31.0f / 255.0f)), 0, 31);
    return uint16_t((r << 11) | (g << 5) | b);
}

void from565(uint16_t v, int out[3]) {
    int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[0] = (r << 3) | (r >> 2);
    out[1] = (g << 2) | (g >> 4);
    out[2] = (b << 3) | (b >> 2);
}

int largestPow2AtMost(int v) {
    int p = 1;
    while (p * 2 <= v) p *= 2;
    return p;
}

} // namespace

Image Image::solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        img.rgba[i * 4 + 0] = r; img.rgba[i * 4 + 1] = g; img.rgba[i * 4 + 2] = b; img.rgba[i * 4 + 3] = a;
    }
    return img;
}

void Image::sample(float u, float v, float out[4]) const {
    if (width <= 0 || height <= 0) {
        out[0] = out[1] = out[2] = out[3] = 255;
        return;
    }
    float fx = u * width - 0.5f, fy = v * height - 0.5f;
    int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    float tx = fx - x0, ty = fy - y0;
    auto wrap = [](int a, int n) { int m = a % n; return m < 0 ? m + n : m; };
    for (int c = 0; c < 4; ++c) {
        float a = px(wrap(x0, width), wrap(y0, height))[c], b = px(wrap(x0 + 1, width), wrap(y0, height))[c];
        float d = px(wrap(x0, width), wrap(y0 + 1, height))[c], e = px(wrap(x0 + 1, width), wrap(y0 + 1, height))[c];
        out[c] = (a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty;
    }
}

Image fitToPowerOfTwo(const Image& src, int maxSize) {
    int w = std::clamp(largestPow2AtMost(std::max(src.width, 1)), 4, maxSize);
    int h = std::clamp(largestPow2AtMost(std::max(src.height, 1)), 4, maxSize);
    // keep aspect ratio when clamping
    if (src.width > 0 && src.height > 0 && (w != src.width || h != src.height)) {
        while (w > 4 && h > 4 && (w > maxSize || h > maxSize)) { w /= 2; h /= 2; }
    }
    if (w == src.width && h == src.height) return src;
    Image out;
    out.width = w;
    out.height = h;
    out.rgba.resize(size_t(w) * h * 4);
    // area-average filter (each destination pixel covers a box of source pixels)
    for (int y = 0; y < h; ++y) {
        float sy0 = float(y) * src.height / h, sy1 = float(y + 1) * src.height / h;
        for (int x = 0; x < w; ++x) {
            float sx0 = float(x) * src.width / w, sx1 = float(x + 1) * src.width / w;
            double acc[4] = {0, 0, 0, 0}, total = 0;
            for (int yy = int(sy0); yy < std::min(int(std::ceil(sy1)), src.height); ++yy) {
                double wy = std::min<double>(yy + 1, sy1) - std::max<double>(yy, sy0);
                for (int xx = int(sx0); xx < std::min(int(std::ceil(sx1)), src.width); ++xx) {
                    double wx = std::min<double>(xx + 1, sx1) - std::max<double>(xx, sx0);
                    double wgt = wx * wy;
                    const uint8_t* p = src.px(xx, yy);
                    for (int c = 0; c < 4; ++c) acc[c] += p[c] * wgt;
                    total += wgt;
                }
            }
            uint8_t* d = out.px(x, y);
            for (int c = 0; c < 4; ++c) d[c] = uint8_t(std::clamp(int(std::lround(acc[c] / std::max(total, 1e-9))), 0, 255));
        }
    }
    return out;
}

Image downsample(const Image& src, bool srgb) {
    Image out;
    out.width = std::max(src.width / 2, 1);
    out.height = std::max(src.height / 2, 1);
    out.rgba.resize(size_t(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const uint8_t* p = src.px(std::min(x * 2 + dx, src.width - 1), std::min(y * 2 + dy, src.height - 1));
                    for (int c = 0; c < 3; ++c) acc[c] += srgb ? srgbToLinear(p[c] / 255.0f) : p[c] / 255.0f;
                    acc[3] += p[3] / 255.0f;
                }
            uint8_t* d = out.px(x, y);
            for (int c = 0; c < 4; ++c) {
                float v = acc[c] / 4.0f;
                if (c < 3 && srgb) v = linearToSrgb(v);
                d[c] = uint8_t(std::clamp(int(std::lround(v * 255.0f)), 0, 255));
            }
        }
    return out;
}

void encodeBC1Block(const uint8_t rgba[64], uint8_t out[8]) {
    bool transparent[16];
    bool anyTransparent = false;
    float mean[3] = {0, 0, 0};
    int opaque = 0;
    for (int i = 0; i < 16; ++i) {
        transparent[i] = rgba[i * 4 + 3] < 128;
        anyTransparent |= transparent[i];
        if (!transparent[i]) {
            for (int c = 0; c < 3; ++c) mean[c] += rgba[i * 4 + c];
            ++opaque;
        }
    }
    if (opaque == 0) {
        // fully transparent block: c0 <= c1 mode, all indices 3
        out[0] = out[1] = out[2] = out[3] = 0;
        out[4] = out[5] = out[6] = out[7] = 0xFF;
        return;
    }
    for (float& m : mean) m /= float(opaque);
    // principal axis via power iteration on the covariance matrix
    float cov[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 16; ++i) {
        if (transparent[i]) continue;
        float d[3] = {rgba[i * 4] - mean[0], rgba[i * 4 + 1] - mean[1], rgba[i * 4 + 2] - mean[2]};
        cov[0] += d[0] * d[0]; cov[1] += d[0] * d[1]; cov[2] += d[0] * d[2];
        cov[3] += d[1] * d[1]; cov[4] += d[1] * d[2]; cov[5] += d[2] * d[2];
    }
    float axis[3] = {1, 1, 1};
    for (int it = 0; it < 8; ++it) {
        float n[3] = {cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2],
                      cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2],
                      cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2]};
        float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l < 1e-6f) break;
        for (int c = 0; c < 3; ++c) axis[c] = n[c] / l;
    }
    float tmin = 1e9f, tmax = -1e9f;
    for (int i = 0; i < 16; ++i) {
        if (transparent[i]) continue;
        float t = (rgba[i * 4] - mean[0]) * axis[0] + (rgba[i * 4 + 1] - mean[1]) * axis[1] + (rgba[i * 4 + 2] - mean[2]) * axis[2];
        tmin = std::min(tmin, t);
        tmax = std::max(tmax, t);
    }
    float e0[3], e1[3];
    for (int c = 0; c < 3; ++c) {
        e0[c] = std::clamp(mean[c] + axis[c] * tmax, 0.0f, 255.0f);
        e1[c] = std::clamp(mean[c] + axis[c] * tmin, 0.0f, 255.0f);
    }
    uint16_t c0 = to565(e0), c1 = to565(e1);
    bool threeColour = anyTransparent;
    if (threeColour) {
        if (c0 > c1) std::swap(c0, c1);
    } else {
        if (c0 < c1) std::swap(c0, c1);
    }
    int p0[3], p1[3];
    from565(c0, p0);
    from565(c1, p1);
    int palette[4][3];
    for (int c = 0; c < 3; ++c) {
        palette[0][c] = p0[c];
        palette[1][c] = p1[c];
        if (!threeColour && c0 != c1) {
            palette[2][c] = (2 * p0[c] + p1[c]) / 3;
            palette[3][c] = (p0[c] + 2 * p1[c]) / 3;
        } else {
            palette[2][c] = (p0[c] + p1[c]) / 2;
            palette[3][c] = 0;
        }
    }
    int usable = (threeColour || c0 == c1) ? 3 : 4;
    uint32_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        if (transparent[i]) {
            best = 3;
        } else {
            int bestD = 1 << 30;
            for (int k = 0; k < usable; ++k) {
                int d = 0;
                for (int c = 0; c < 3; ++c) {
                    int e = rgba[i * 4 + c] - palette[k][c];
                    d += e * e;
                }
                if (d < bestD) { bestD = d; best = k; }
            }
        }
        indices |= uint32_t(best) << (2 * i);
    }
    std::memcpy(out, &c0, 2);
    std::memcpy(out + 2, &c1, 2);
    std::memcpy(out + 4, &indices, 4);
}

void decodeBC1Block(const uint8_t in[8], uint8_t rgba[64]) {
    uint16_t c0, c1;
    uint32_t idx;
    std::memcpy(&c0, in, 2);
    std::memcpy(&c1, in + 2, 2);
    std::memcpy(&idx, in + 4, 4);
    int p0[3], p1[3];
    from565(c0, p0);
    from565(c1, p1);
    int pal[4][4];
    for (int c = 0; c < 3; ++c) {
        pal[0][c] = p0[c];
        pal[1][c] = p1[c];
        if (c0 > c1) {
            pal[2][c] = (2 * p0[c] + p1[c]) / 3;
            pal[3][c] = (p0[c] + 2 * p1[c]) / 3;
        } else {
            pal[2][c] = (p0[c] + p1[c]) / 2;
            pal[3][c] = 0;
        }
    }
    pal[0][3] = pal[1][3] = pal[2][3] = 255;
    pal[3][3] = c0 > c1 ? 255 : 0;
    for (int i = 0; i < 16; ++i) {
        int k = (idx >> (2 * i)) & 3;
        for (int c = 0; c < 4; ++c) rgba[i * 4 + c] = uint8_t(pal[k][c]);
    }
}

std::vector<uint8_t> buildTex(const Image& src, TexFormat format, bool srgbMips) {
    std::vector<Image> mips;
    mips.push_back(src);
    while (mips.back().width > 4 && mips.back().height > 4) mips.push_back(downsample(mips.back(), srgbMips));

    const bool bc1 = format == TEX_BC1_UNORM || format == TEX_BC1_UNORM_SRGB;
    std::vector<std::vector<uint8_t>> payloads;
    for (const Image& m : mips) {
        std::vector<uint8_t> p;
        if (bc1) {
            int bw = (m.width + 3) / 4, bh = (m.height + 3) / 4;
            p.resize(size_t(bw) * bh * 8);
            for (int by = 0; by < bh; ++by)
                for (int bx = 0; bx < bw; ++bx) {
                    uint8_t block[64];
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 4; ++x) {
                            const uint8_t* s = m.px(std::min(bx * 4 + x, m.width - 1), std::min(by * 4 + y, m.height - 1));
                            std::memcpy(block + (y * 4 + x) * 4, s, 4);
                        }
                    encodeBC1Block(block, &p[(size_t(by) * bw + bx) * 8]);
                }
        } else {
            p = m.rgba;
        }
        payloads.push_back(std::move(p));
    }

    // Header layout reproduced from Jodo's MHW Tex converter (DDS -> TEX direction).
    ByteWriter w;
    const uint32_t width = uint32_t(src.width), height = uint32_t(src.height);
    w.put<uint32_t>(0x00584554); // "TEX\0"
    w.put<uint32_t>(0x10);
    w.put<uint32_t>(0);
    w.put<uint32_t>(0);
    w.put<uint32_t>(2);
    w.put<uint32_t>(uint32_t(mips.size()));
    w.put<uint32_t>(width);
    w.put<uint32_t>(height);
    w.put<uint32_t>(1);
    w.put<uint32_t>(format);
    // fixed unknown block: 01 00 00 00 | 0 | 0 | 0 | FFFFFFFF | 0 | 0
    w.put<uint32_t>(1);
    w.put<uint32_t>(0);
    w.put<uint32_t>(0);
    w.put<uint32_t>(0);
    w.put<uint32_t>(0xFFFFFFFF);
    w.put<uint32_t>(0);
    w.put<uint32_t>(0);
    w.put<uint32_t>(0); // 1 only for BC6H/BC7
    for (int i = 0; i < 4; ++i) w.put<uint32_t>(0);
    for (int i = 0; i < 8; ++i) w.put<int32_t>(-1);
    const bool fullWidth = !bc1; // raw formats store the full row width
    const uint16_t pitchField = uint16_t(fullWidth ? width : width / 2);
    w.put<uint32_t>(width);
    w.put<uint16_t>(pitchField);
    w.put<uint16_t>(uint16_t(width));
    w.zeros(8);
    w.put<uint16_t>(pitchField);
    w.put<uint16_t>(uint16_t(width));
    w.zeros(8);
    w.put<uint16_t>(pitchField);
    w.put<uint16_t>(uint16_t(width));
    w.zeros(32);
    if (w.tell() != 0xB8) throw FormatError("internal: tex header size");
    uint64_t offset = 0xB8 + 8 * mips.size();
    for (const auto& p : payloads) {
        w.put<uint64_t>(offset);
        offset += p.size();
    }
    for (const auto& p : payloads) w.write(p.data(), p.size());
    return w.buf;
}

TexInfo parseTexHeader(const std::vector<uint8_t>& data) {
    ByteReader r(data);
    if (r.get<uint32_t>() != 0x00584554) throw FormatError("not a tex file");
    TexInfo t;
    r.seek(0x14);
    t.mips = int(r.get<uint32_t>());
    t.width = int(r.get<uint32_t>());
    t.height = int(r.get<uint32_t>());
    r.seek(0x24);
    t.format = r.get<uint32_t>();
    r.seek(0xB8);
    for (int i = 0; i < t.mips; ++i) t.mipOffsets.push_back(r.get<uint64_t>());
    return t;
}

} // namespace vh
