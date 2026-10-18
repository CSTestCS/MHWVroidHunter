// vroidhunter-cli: offline tools for MHW VRoid Hunter.
//
//   vroidhunter-cli auto    --game <MHW folder>   (convert the newest .vrm in <game>/VRoidHunter)
//   vroidhunter-cli apply   --game <MHW folder> --vrm <file.vrm> [options]
//   vroidhunter-cli convert --templates <extracted chunk folder> --out <nativePC> --vrm <file.vrm> [options]
//   vroidhunter-cli remove  --game <MHW folder>
//   vroidhunter-cli preview --vrm <file.vrm> --png <out.png> [--templates <folder>] [--conform 1]
//   vroidhunter-cli inspect <file.mod3|file.mrl3|file.tex>
//   vroidhunter-cli list-chunk <chunkGx.bin>
//
// Options: --conform <0..1>  --show-armor helm,body,arm,wst,leg  --keep-transparent  --max-texture <n>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <stb_image_write.h>

#include "autoconvert.h"
#include "binio.h"
#include "builder.h"
#include "chunk.h"
#include "fsutil.h"
#include "mod3.h"
#include "mrl3.h"
#include "retarget.h"
#include "tex.h"
#include "vrm.h"

using namespace vh;

namespace {

struct Args {
    std::string cmd;
    std::vector<std::string> positional;
    std::map<std::string, std::string> opt;
    bool has(const std::string& k) const { return opt.count(k) != 0; }
    std::string get(const std::string& k, const std::string& d = "") const {
        auto it = opt.find(k);
        return it == opt.end() ? d : it->second;
    }
};

Args parseArgs(const std::vector<std::string>& argv) {
    Args a;
    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string& s = argv[i];
        if (a.cmd.empty() && s.rfind("--", 0) != 0) { a.cmd = s; continue; }
        if (s.rfind("--", 0) == 0) {
            std::string k = s.substr(2);
            if (i + 1 < argv.size() && argv[i + 1].rfind("--", 0) != 0 && k != "keep-transparent") a.opt[k] = argv[++i];
            else a.opt[k] = "1";
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

Settings settingsFrom(const Args& a) {
    Settings s;
    s.enabled = true;
    s.vrmPath = a.get("vrm");
    s.conform = clamp01(float(std::atof(a.get("conform", "1").c_str())));
    s.dropTransparent = !a.has("keep-transparent");
    s.maxTextureSize = std::atoi(a.get("max-texture", "2048").c_str());
    std::string show = a.get("show-armor");
    for (int i = 0; i < kSlotCount; ++i) s.showArmor[i] = show.find(slotFolder(Slot(i))) != std::string::npos;
    return s;
}

int printResult(const ApplyResult& r) {
    for (auto& l : r.log) std::cout << "  " << l << "\n";
    std::cout << (r.ok ? "OK: " : "FAILED: ") << r.message << "\n";
    return r.ok ? 0 : 1;
}

// ---------------------------------------------------------------- preview renderer
struct Raster {
    int w, h;
    std::vector<float> z;
    std::vector<uint8_t> rgb;
    Raster(int w_, int h_) : w(w_), h(h_), z(size_t(w_) * h_, 1e30f), rgb(size_t(w_) * h_ * 3, 40) {}
    void tri(const Vec3 p[3], const Vec2 uv[3], const Image* img, float shade) {
        float minx = std::min({p[0].x, p[1].x, p[2].x}), maxx = std::max({p[0].x, p[1].x, p[2].x});
        float miny = std::min({p[0].y, p[1].y, p[2].y}), maxy = std::max({p[0].y, p[1].y, p[2].y});
        float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
        if (std::fabs(area) < 1e-9f) return;
        for (int y = std::max(0, int(miny)); y <= std::min(h - 1, int(maxy)); ++y)
            for (int x = std::max(0, int(minx)); x <= std::min(w - 1, int(maxx)); ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((p[1].x - px) * (p[2].y - py) - (p[2].x - px) * (p[1].y - py)) / area;
                float w1 = ((p[2].x - px) * (p[0].y - py) - (p[0].x - px) * (p[2].y - py)) / area;
                float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float depth = w0 * p[0].z + w1 * p[1].z + w2 * p[2].z;
                size_t i = size_t(y) * w + x;
                if (depth >= z[i]) continue;
                float c[4] = {200, 200, 200, 255};
                if (img) img->sample(w0 * uv[0].x + w1 * uv[1].x + w2 * uv[2].x, w0 * uv[0].y + w1 * uv[1].y + w2 * uv[2].y, c);
                if (c[3] < 128) continue;
                z[i] = depth;
                for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = uint8_t(std::min(255.0f, c[k] * shade));
            }
    }
};

int preview(const Args& a) {
    VrmModel vrm = loadVrm(a.get("vrm"));
    HunterSkeleton hs;
    if (a.has("templates")) {
        auto src = openFolderSource(a.get("templates"));
        std::vector<Mod3File> t;
        for (auto& p : src->list())
            if (p.find("\\body\\mod\\") != std::string::npos && p.size() > 5 && p.substr(p.size() - 5) == ".mod3") {
                t.push_back(Mod3File::parse(src->read(p)));
                break;
            }
        if (t.empty()) throw std::runtime_error("no body template found");
        hs = buildHunterSkeleton(t);
    } else {
        hs = syntheticHunterSkeleton();
    }
    RetargetOptions ro;
    ro.conform = clamp01(float(std::atof(a.get("conform", "1").c_str())));
    RetargetResult rr = retargetToHunter(vrm, hs, ro);
    std::printf("scale %.2f, vrm height %.3f m, hunter height %.2f, mapped bones %d, meshes %zu, dropped %d, mirrored %d\n",
                rr.report.scale, rr.report.vrmHeight, rr.report.hunterHeight, rr.report.mappedBones, rr.meshes.size(),
                rr.report.droppedTriangles, rr.report.mirroredAxes ? 1 : 0);

    // camera frame from the hunter skeleton
    Vec3 up = normalize(hs.pos(pf::Head) - (hs.pos(pf::FootL) + hs.pos(pf::FootR)) * 0.5f);
    Vec3 left = normalize(hs.pos(pf::ThighL) - hs.pos(pf::ThighR));
    left = normalize(left - up * dot(left, up));
    Vec3 fwd = cross(left, up);
    if (rr.report.mirroredAxes) fwd = -fwd;
    Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    for (auto& m : rr.meshes)
        for (auto& v : m.vertices)
            for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], v.pos[k]); mx[k] = std::max(mx[k], v.pos[k]); }
    Vec3 c = (mn + mx) * 0.5f;
    float ext = length(mx - mn) * 0.55f;
    const int W = 512, H = 512, views = 3;
    std::vector<uint8_t> canvas(size_t(W) * views * H * 3, 0);
    const Vec3 rights[views] = {-left, fwd, left}; // front view, side view, back view
    const Vec3 toCams[views] = {fwd, left, -fwd};
    Vec3 light = normalize(fwd + up * 0.5f + left * 0.3f);
    for (int vi = 0; vi < views; ++vi) {
        Raster r(W, H);
        auto project = [&](const Vec3& p) {
            Vec3 d = p - c;
            return Vec3{(dot(d, rights[vi]) / ext * 0.5f + 0.5f) * W, (0.5f - dot(d, up) / ext * 0.5f) * H, -dot(d, toCams[vi])};
        };
        for (auto& m : rr.meshes) {
            const VrmMaterial& mat = vrm.materials[size_t(m.material)];
            const Image* img = mat.image >= 0 ? &vrm.images[size_t(mat.image)] : nullptr;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
                Vec3 p[3];
                Vec2 uv[3];
                Vec3 n;
                for (int k = 0; k < 3; ++k) {
                    const OutVertex& v = m.vertices[m.indices[t + size_t(k)]];
                    p[k] = project(v.pos);
                    uv[k] = v.uv;
                    n += v.normal;
                }
                float shade = 0.45f + 0.55f * std::max(0.0f, dot(normalize(n), light));
                r.tri(p, uv, img, shade);
            }
        }
        // draw hunter joints as red dots
        for (size_t b = 0; b < hs.world.size(); ++b) {
            Vec3 p = project(hs.world[b].translation());
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    int x = int(p.x) + dx, y = int(p.y) + dy;
                    if (x < 0 || y < 0 || x >= W || y >= H) continue;
                    size_t i = (size_t(y) * W + x) * 3;
                    r.rgb[i] = 255; r.rgb[i + 1] = 30; r.rgb[i + 2] = 30;
                }
        }
        for (int y = 0; y < H; ++y)
            std::memcpy(&canvas[(size_t(y) * W * views + size_t(vi) * W) * 3], &r.rgb[size_t(y) * W * 3], size_t(W) * 3);
    }
    std::string out = a.get("png", "preview.png");
    if (!stbi_write_png(out.c_str(), W * views, H, 3, canvas.data(), W * views * 3)) throw std::runtime_error("could not write png");
    std::printf("wrote %s\n", out.c_str());
    return 0;
}

int inspect(const std::string& path) {
    auto data = readFile(path);
    if (path.size() > 5 && path.substr(path.size() - 5) == ".mod3") {
        Mod3File f = Mod3File::parse(data);
        std::printf("mod3 v%d: %zu bones, %zu meshes, %zu materials, %zu groups, trailing %zu bytes\n", f.version,
                    f.bones.size(), f.meshes.size(), f.materials.size(), f.groups.size(), f.trailing.size());
        for (auto& m : f.materials) std::printf("  material %s (jamcrc %08x)\n", m.c_str(), jamcrc(m));
        for (size_t i = 0; i < f.bones.size(); ++i) {
            Vec3 p = f.worldMatrix(int(i)).translation();
            std::printf("  bone %3zu fn %3d parent %3d sym %3d world (%.2f %.2f %.2f)\n", i, f.bones[i].function,
                        f.bones[i].parent, f.bones[i].symmetric, p.x, p.y, p.z);
        }
        for (size_t i = 0; i < f.meshes.size(); ++i) {
            const auto& m = f.meshes[i];
            const VertexLayout* L = findLayout(m.h.blocktype);
            std::printf("  mesh %zu: %zu verts %zu tris %s mat %d lod %d vis %d shadow %d boxes %zu\n", i, m.vertices.size(),
                        m.indices.size() / 3, L ? L->name : "?", m.h.materialIdx, m.h.lod, m.h.visibleCondition,
                        m.h.shadowCast, m.boxes.size());
        }
    } else if (path.size() > 5 && path.substr(path.size() - 5) == ".mrl3") {
        Mrl3File f = Mrl3File::parse(data);
        std::printf("mrl3: %zu textures, %zu materials\n", f.textures.size(), f.materials.size());
        for (auto& t : f.textures) std::printf("  tex %08x %s\n", t.typeCode, t.path.c_str());
        for (auto& m : f.materials) {
            std::printf("  material hash %08x shader %08x size %u bindings %d\n", m.h.materialNameHash, m.h.shaderHash,
                        m.h.matSize, m.bindingCount());
            for (int i = 0; i < m.bindingCount(); ++i) {
                Mrl3Binding b = m.binding(i);
                std::printf("    type %d map %05x tex %u\n", b.resourceType & 0xF, bindingMapTypeId(b), b.texIdx);
            }
        }
    } else {
        TexInfo t = parseTexHeader(data);
        std::printf("tex: format %u %dx%d, %d mips\n", t.format, t.width, t.height, t.mips);
    }
    return 0;
}

int realMain(const std::vector<std::string>& argv) {
    Args a = parseArgs(argv);
    if (a.cmd == "preview") return preview(a);
    if (a.cmd == "inspect" && !a.positional.empty()) return inspect(a.positional[0]);
    if (a.cmd == "list-chunk" && !a.positional.empty()) {
        ChunkArchive arch(a.positional[0], true, makeDecompressor(""));
        auto files = arch.listFiles(a.has("all") ? nullptr : std::function<bool(const std::string&)>(isRelevantGamePath));
        for (auto& f : files) std::printf("%s\t%llu\n", f.path.c_str(), (unsigned long long)f.size);
        std::printf("%zu files\n", files.size());
        return 0;
    }
    if (a.cmd == "auto") {
        // same as the game does at start-up: convert whatever is in <game>/VRoidHunter
        std::string game = a.get("game");
        if (game.empty()) throw std::runtime_error("--game <Monster Hunter World folder> is required");
        JobResult r = runJob(game, JobMode::Auto, [](const std::string& m) { std::cout << m << "\n"; });
        std::cout << (r.ok ? "OK: " : "FAILED: ") << r.message << "\n";
        return r.ok ? 0 : 1;
    }
    if (a.cmd == "apply" || a.cmd == "remove") {
        std::string game = a.get("game");
        if (game.empty()) throw std::runtime_error("--game <Monster Hunter World folder> is required");
        std::string data = join(game, "nativePC/plugins/MHWVroidHunter");
        makeDirs(data);
        ModManager mm(join(game, "nativePC"), data, [&] {
            return openArchiveSource(game, join(data, "game_index.cache"), [](const std::string& m) { std::cout << m << "\n"; });
        });
        if (a.cmd == "remove") return printResult(mm.removeAll());
        Settings s = settingsFrom(a);
        return printResult(mm.apply(s, [](const std::string& m) { std::cout << m << "\n"; }));
    }
    if (a.cmd == "convert") {
        std::string out = a.get("out"), tpl = a.get("templates");
        if (out.empty() || tpl.empty()) throw std::runtime_error("--templates and --out are required");
        std::string data = join(out, "plugins/MHWVroidHunter");
        makeDirs(data);
        ModManager mm(out, data, [&] { return openFolderSource(tpl); });
        Settings s = settingsFrom(a);
        return printResult(mm.apply(s));
    }
    std::cout << "usage: vroidhunter-cli apply|convert|remove|preview|inspect|list-chunk ... (see source header)\n";
    return 2;
}

} // namespace

#if defined(_WIN32)
#include <windows.h>
int wmain(int argc, wchar_t** wargv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(toU8(std::filesystem::path(wargv[i])));
    try {
        return realMain(args);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    try {
        return realMain(args);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
#endif
