#include "builder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "binio.h"
#include "fsutil.h"
#include "mod3.h"
#include "mrl3.h"
#include "tex.h"
#include "vrm.h"

namespace fs = std::filesystem;

namespace vh {

namespace {

constexpr const char* kModVersion = "1";
constexpr uint32_t kSkin4wt1UV = 0xf637401c;

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool endsWith(const std::string& s, const char* e) {
    size_t n = std::strlen(e);
    return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

std::string toNativeRel(const std::string& gamePath) {
    std::string s = gamePath;
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", v);
    return b;
}

// pl\<g>_equip\<set>\<slot>\mod\<file>
struct ArmorPath {
    char gender = 0;
    std::string set;
    int slot = -1;
    std::string file;
};

bool parseArmorPath(const std::string& p, ArmorPath& out) {
    auto parts = split(p, '\\');
    if (parts.size() != 6 || parts[0] != "pl" || parts[4] != "mod") return false;
    if (parts[1] == "f_equip") out.gender = 'f';
    else if (parts[1] == "m_equip") out.gender = 'm';
    else return false;
    out.set = parts[2];
    out.slot = -1;
    for (int i = 0; i < kSlotCount; ++i)
        if (parts[3] == slotFolder(Slot(i))) out.slot = i;
    out.file = parts[5];
    return out.slot >= 0;
}

bool isFaceOrHair(const std::string& p, char gender) {
    if (startsWith(p, "pl\\hair\\")) return true;
    return gender == 'f' ? startsWith(p, "pl\\f_face\\") : startsWith(p, "pl\\m_face\\");
}

// ---------------------------------------------------------------- sources
class ArchiveSource : public GameSource {
public:
    ArchiveSource(const std::string& gameDir, const std::string& cacheFile,
                  const std::function<void(const std::string&)>& progress)
        : arch_(gameDir) {
        std::string fp = std::string("v") + kModVersion + "|" + arch_.fingerprint();
        if (!loadCache(cacheFile, fp)) {
            arch_.index(isRelevantGamePath, progress);
            saveCache(cacheFile, fp);
        }
    }
    std::vector<std::string> list() override {
        std::vector<std::string> v;
        for (auto& kv : arch_.files()) v.push_back(kv.first);
        return v;
    }
    std::vector<uint8_t> read(const std::string& path) override { return arch_.read(path); }

private:
    bool loadCache(const std::string& file, const std::string& fp) {
        if (!exists(file)) return false;
        try {
            auto raw = readFile(file);
            std::istringstream in(std::string(raw.begin(), raw.end()));
            std::string line;
            if (!std::getline(in, line) || line != fp) return false;
            std::map<std::string, GameArchives::Location> files;
            while (std::getline(in, line)) {
                auto f = split(line, '\t');
                if (f.size() != 4) continue;
                files[f[0]] = GameArchives::Location{std::atoi(f[1].c_str()), std::strtoull(f[2].c_str(), nullptr, 10),
                                                     std::strtoull(f[3].c_str(), nullptr, 10)};
            }
            if (files.empty()) return false;
            arch_.setFiles(std::move(files));
            return true;
        } catch (...) {
            return false;
        }
    }
    void saveCache(const std::string& file, const std::string& fp) {
        std::ostringstream o;
        o << fp << "\n";
        for (auto& kv : arch_.files())
            o << kv.first << "\t" << kv.second.archive << "\t" << kv.second.offset << "\t" << kv.second.size << "\n";
        std::string s = o.str();
        try {
            writeFile(file, std::vector<uint8_t>(s.begin(), s.end()));
        } catch (...) {
        }
    }
    GameArchives arch_;
};

class FolderSource : public GameSource {
public:
    explicit FolderSource(const std::string& folder) {
        std::error_code ec;
        fs::path root = u8(folder);
        for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file()) continue;
            std::string rel = normalizeGamePath(toU8(fs::relative(it->path(), root)));
            if (isRelevantGamePath(rel)) files_[rel] = toU8(it->path());
        }
    }
    std::vector<std::string> list() override {
        std::vector<std::string> v;
        for (auto& kv : files_) v.push_back(kv.first);
        return v;
    }
    std::vector<uint8_t> read(const std::string& path) override {
        auto it = files_.find(normalizeGamePath(path));
        if (it == files_.end()) throw std::runtime_error("file not found in folder source: " + path);
        return readFile(it->second);
    }

private:
    std::map<std::string, std::string> files_;
};

// ---------------------------------------------------------------- materials
struct TemplateMaterial {
    Mrl3File file;
    Mrl3Material material;
    bool maskedHash = false;
    int score = -1000;
};

const char* const kKnownMapNames[] = {
    "tAlbedoMap", "tNormalMap", "tRMTMap", "tEmissiveMap", "tColorMaskMap", "tFxMap", "tDetailNormalMap",
    "tAddNormalMap", "tAddNormalMaskMap", "tAlphaMap", "tOpacityMap", "tFurMap", "tPartsMaskMap", "tMaskMap",
    "tTranslucencyMap", "tSkinMap", "tPatternMap", "tAlbedoBlendMap", "tNormalBlendMap", "tRMTBlendMap",
    "tEmissiveBlendMap", "tMaterialBlendMap", "tDetailMapA", "tDetailMapB", "tDetailMapC", "tDetailMapD", "tBaseMap",
    "tFurNormalMap", "tFurMaskMap", "tSphereMap", "tCubeMap", "tFlowMap", "tDisplacementMap", "tHeightMap",
};

std::string mapName(uint32_t id) {
    for (const char* n : kKnownMapNames) {
        if (mapTypeId(n) == id) return n;
        if (mapTypeId(std::string(n) + "__disclosure") == id) return n;
    }
    return "";
}

bool hasCBuffer(const Mrl3Material& m, const char* name) {
    uint32_t id = mapTypeId(name);
    for (int i = 0; i < m.bindingCount(); ++i) {
        Mrl3Binding b = m.binding(i);
        if ((b.resourceType & 0xF) == 0 && bindingMapTypeId(b) == id) return true;
    }
    return false;
}

int scoreMaterial(const Mrl3Material& m) {
    int score = 0;
    bool albedo = false;
    for (int i = 0; i < m.bindingCount(); ++i) {
        Mrl3Binding b = m.binding(i);
        if ((b.resourceType & 0xF) != 2) continue;
        std::string n = mapName(bindingMapTypeId(b));
        if (n == "tAlbedoMap" || n == "tBaseMap") { score += 10; albedo = true; }
        else if (n == "tNormalMap") score += 3;
        else if (n == "tRMTMap") score += 3;
        else if (n == "tFurMap" || n == "tSkinMap" || n == "tFurNormalMap") score -= 30;
        else score -= 1; // extra maps we must neutralise
    }
    if (!albedo) score -= 100;
    for (const char* bad : {"CBMhMaterialPLSkinLocal__disclosure", "CBMhMaterialFurLocal__disclosure",
                            "CBMhMaterialPLHairLocal__disclosure", "CBMhMaterialPLEyeLocal__disclosure",
                            "CBMhMaterialPLEditFaceLocal__disclosure", "CBMhMaterialStdBlendLocal__disclosure"})
        if (hasCBuffer(m, bad)) score -= 40;
    for (const char* good : {"CBMhMaterialPLLocal__disclosure", "CBMhMaterialStdLocal__disclosure"})
        if (hasCBuffer(m, good)) score += 5;
    return score;
}

uint32_t materialHash(const std::string& name, bool masked) {
    uint32_t h = jamcrc(name);
    return masked ? (h & 0x7fffffff) : h;
}

// Texture role for a binding name.
enum class TexRole { Albedo, Normal, RMT, White, Black };
TexRole roleFor(const std::string& n) {
    if (n == "tAlbedoMap" || n == "tBaseMap") return TexRole::Albedo;
    if (n == "tRMTMap") return TexRole::RMT;
    if (n.find("Normal") != std::string::npos && n.find("Mask") == std::string::npos) return TexRole::Normal;
    if (n == "tAlphaMap" || n == "tOpacityMap") return TexRole::White;
    return TexRole::Black;
}

// ---------------------------------------------------------------- textures
float s2l(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float l2s(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

Image bakeAlbedo(const VrmModel& vrm, const VrmMaterial& m, int maxSize) {
    Image img = (m.image >= 0 && m.image < int(vrm.images.size())) ? vrm.images[size_t(m.image)] : Image::solid(4, 4, 255, 255, 255, 255);
    img = fitToPowerOfTwo(img, maxSize);
    const float f[4] = {m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w};
    const bool tint = std::fabs(f[0] - 1) > 1e-3f || std::fabs(f[1] - 1) > 1e-3f || std::fabs(f[2] - 1) > 1e-3f;
    uint8_t lut[3][256];
    for (int c = 0; c < 3; ++c)
        for (int v = 0; v < 256; ++v)
            lut[c][v] = uint8_t(std::clamp(int(std::lround(l2s(s2l(v / 255.0f) * f[c]) * 255.0f)), 0, 255));
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        if (tint)
            for (int c = 0; c < 3; ++c) img.rgba[i + size_t(c)] = lut[c][img.rgba[i + size_t(c)]];
        float a = img.rgba[i + 3] / 255.0f * f[3];
        bool on = m.alphaMode == AlphaMode::Opaque ? true
                  : m.alphaMode == AlphaMode::Mask ? a >= m.alphaCutoff
                                                   : a >= 0.5f;
        img.rgba[i + 3] = on ? 255 : 0;
    }
    return img;
}

} // namespace

bool isRelevantGamePath(const std::string& p) {
    if (!endsWith(p, ".mod3") && !endsWith(p, ".mrl3")) return false;
    return startsWith(p, "pl\\f_equip\\") || startsWith(p, "pl\\m_equip\\") || startsWith(p, "pl\\hair\\") ||
           startsWith(p, "pl\\f_face\\") || startsWith(p, "pl\\m_face\\");
}

std::unique_ptr<GameSource> openArchiveSource(const std::string& gameDir, const std::string& cacheFile,
                                              const std::function<void(const std::string&)>& progress) {
    return std::make_unique<ArchiveSource>(gameDir, cacheFile, progress);
}

std::unique_ptr<GameSource> openFolderSource(const std::string& folder) { return std::make_unique<FolderSource>(folder); }

// ---------------------------------------------------------------- ModManager

ModManager::ModManager(std::string nativePC, std::string dataDir, std::function<std::unique_ptr<GameSource>()> sourceFactory)
    : nativePC_(std::move(nativePC)), dataDir_(std::move(dataDir)), sourceFactory_(std::move(sourceFactory)) {
    loadManifest();
}

std::string ModManager::manifestPath() const { return join(dataDir_, "installed_files.txt"); }

void ModManager::loadManifest() {
    manifest_.clear();
    if (!exists(manifestPath())) return;
    auto raw = readFile(manifestPath());
    std::istringstream in(std::string(raw.begin(), raw.end()));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) manifest_.insert(line);
    }
}

void ModManager::saveManifest() const {
    std::string s;
    for (auto& p : manifest_) s += p + "\n";
    writeFile(manifestPath(), std::vector<uint8_t>(s.begin(), s.end()));
}

ApplyResult ModManager::removeAll() {
    ApplyResult r;
    int removed = 0;
    std::set<std::string> dirs;
    for (auto& rel : manifest_) {
        if (removeFile(join(nativePC_, rel))) ++removed;
        for (fs::path d = u8(rel).parent_path(); !d.empty(); d = d.parent_path()) dirs.insert(toU8(d));
    }
    // remove folders that are now empty (deepest first); folders with other mods' files stay
    std::vector<std::string> ordered(dirs.begin(), dirs.end());
    std::sort(ordered.begin(), ordered.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    for (auto& d : ordered) {
        std::error_code ec;
        fs::path p = u8(join(nativePC_, d));
        if (fs::is_directory(p, ec) && fs::is_empty(p, ec)) fs::remove(p, ec);
    }
    manifest_.clear();
    saveManifest();
    removeFile(join(dataDir_, "links_complete.txt"));
    // tidy up our own folder
    std::error_code ec;
    fs::remove_all(u8(join(nativePC_, "vroidhunter")), ec);
    r.ok = true;
    r.message = "Removed " + std::to_string(removed) + " mod files. The normal hunter is back.";
    return r;
}

namespace {

struct BuiltSlotFiles {
    std::string mod3, mrl3; // relative to nativePC
};

Mod3File makeSlotMod3(const HunterSkeleton& hs, const std::vector<const OutMesh*>& meshes,
                      const std::vector<std::string>& materialNames, const std::map<int, int>& vrmMatToLocal) {
    Mod3File f = hs.base;
    f.meshes.clear();
    f.materials = materialNames;
    auto toByte = [](float v) { return int8_t(std::clamp(int(std::lround(v * 127.0f)), -127, 127)); };
    for (const OutMesh* om : meshes) {
        Mod3Mesh m;
        m.h = hs.meshDefaults;
        m.h.blocktype = kSkin4wt1UV;
        m.h.materialIdx = uint16_t(vrmMatToLocal.at(om->material));
        for (const OutVertex& ov : om->vertices) {
            Mod3Vertex v;
            v.pos = ov.pos;
            v.normal[0] = toByte(ov.normal.x);
            v.normal[1] = toByte(ov.normal.y);
            v.normal[2] = toByte(ov.normal.z);
            v.normal[3] = hs.normalW;
            v.tangent[0] = toByte(ov.tangent.x);
            v.tangent[1] = toByte(ov.tangent.y);
            v.tangent[2] = toByte(ov.tangent.z);
            v.tangent[3] = int8_t(ov.tangent.w < 0 ? -hs.tangentWPositive : hs.tangentWPositive);
            v.uv[0][0] = ov.uv.x;
            v.uv[0][1] = ov.uv.y;
            for (int k = 0; k < 4; ++k) {
                v.bones[k] = ov.bones[k];
                v.weights[k] = ov.weights[k];
            }
            m.vertices.push_back(v);
        }
        for (uint32_t i : om->indices) m.indices.push_back(uint16_t(i));
        m.boxes = computeMeshBoxes(m, hs.world);
        f.meshes.push_back(std::move(m));
    }
    if (f.meshes.empty()) {
        // Invisible placeholder: one zero-area triangle at the head.
        Mod3Mesh m;
        m.h = hs.meshDefaults;
        m.h.blocktype = kSkin4wt1UV;
        m.h.materialIdx = 0;
        int head = std::max(0, hs.bone(pf::Head));
        Vec3 p = hs.world[size_t(head)].translation();
        for (int k = 0; k < 3; ++k) {
            Mod3Vertex v;
            v.pos = p;
            v.bones[0] = v.bones[1] = v.bones[2] = v.bones[3] = uint8_t(head);
            v.weights[0] = 1;
            m.vertices.push_back(v);
            m.indices.push_back(uint16_t(k));
        }
        m.boxes = computeMeshBoxes(m, hs.world);
        f.meshes.push_back(std::move(m));
        if (f.materials.empty()) f.materials.push_back("vh_empty");
    }
    f.computeBounds();
    return f;
}

struct TexturePaths {
    std::map<int, std::string> albedo; // VRM material -> resource path
    std::string normal, rmt, white, black;
};

Mrl3File makeSlotMrl3(const TemplateMaterial& tm, const std::vector<std::pair<std::string, int>>& mats, const TexturePaths& tex,
                      std::vector<std::string>& log) {
    Mrl3File f;
    f.magic = tm.file.magic;
    f.unkn = tm.file.unkn;
    Mrl3Texture proto;
    if (!tm.file.textures.empty()) {
        proto = tm.file.textures[0];
    } else {
        proto.typeCode = jamcrc(std::string("rTexture")) & 0x7fffffff;
    }
    std::map<std::string, uint32_t> texIndex; // path -> 1-based index
    auto addTex = [&](const std::string& path) {
        auto it = texIndex.find(path);
        if (it != texIndex.end()) return it->second;
        Mrl3Texture t = proto;
        t.path = path;
        f.textures.push_back(t);
        uint32_t idx = uint32_t(f.textures.size());
        texIndex[path] = idx;
        return idx;
    };
    for (auto& [name, vrmMat] : mats) {
        Mrl3Material m = tm.material;
        m.h.materialNameHash = materialHash(name, tm.maskedHash);
        bool gotAlbedo = false;
        for (int i = 0; i < m.bindingCount(); ++i) {
            Mrl3Binding b = m.binding(i);
            if ((b.resourceType & 0xF) != 2) continue;
            std::string role = mapName(bindingMapTypeId(b));
            TexRole r = roleFor(role);
            std::string path;
            switch (r) {
            case TexRole::Albedo: {
                auto it = tex.albedo.find(vrmMat);
                path = it != tex.albedo.end() ? it->second : tex.white;
                gotAlbedo = true;
                break;
            }
            case TexRole::Normal: path = tex.normal; break;
            case TexRole::RMT: path = tex.rmt; break;
            case TexRole::White: path = tex.white; break;
            case TexRole::Black: path = tex.black; break;
            }
            b.texIdx = addTex(path);
            m.setBinding(i, b);
        }
        if (!gotAlbedo) log.push_back("warning: template material has no albedo slot; colours may be missing");
        f.materials.push_back(m);
    }
    return f;
}

} // namespace

bool ModManager::buildModel(const Settings& s, GameSource& src, const std::vector<std::string>& files, const std::string& modelDir,
                            const std::string& texTag, const std::string& doneKey, std::vector<std::string>& log,
                            const std::function<void(const std::string&)>& progress) {
    auto say = [&](const std::string& m) {
        log.push_back(m);
        if (progress) progress(m);
    };
    say("Loading VRM model...");
    VrmModel vrm = loadVrm(s.vrmPath);
    say("Loaded \"" + (vrm.title.empty() ? std::string("VRM") : vrm.title) + "\" (VRM " + (vrm.vrmVersion ? "1.0" : "0.x") + ", " +
        std::to_string(vrm.primitives.size()) + " mesh parts)");

    // collect armour folders per gender/set
    std::map<char, std::map<std::string, std::map<int, std::vector<std::string>>>> sets; // g -> set -> slot -> files
    for (auto& p : files) {
        ArmorPath ap;
        if (parseArmorPath(p, ap)) sets[ap.gender][ap.set][ap.slot].push_back(p);
    }

    // shared textures
    // textures get a new folder per model so the game never shows a cached old texture
    const std::string texDirRes = modelDir + "\\tex_" + texTag + "\\";
    TexturePaths tex;
    tex.normal = texDirRes + "flat_nm";
    tex.rmt = texDirRes + "matte_rmt";
    tex.white = texDirRes + "white";
    tex.black = texDirRes + "black";
    auto writeRes = [&](const std::string& resPath, const std::string& ext, const std::vector<uint8_t>& data) {
        std::string rel = toNativeRel(resPath) + ext;
        addToManifest(rel);
        if (ext == ".mod3" || ext == ".mrl3") {
            // Model/material masters keep their path: rewriting them in place updates every
            // armour path hard-linked to them at once, so a model change needs no re-linking.
            writeFileInPlace(join(nativePC_, rel), data);
            std::string base = toNativeRel(resPath);
            for (int k = 1; k < 64; ++k) { // extra copies made when a file reached the hard link limit
                std::string copy = base + "~" + std::to_string(k) + ext;
                if (!exists(join(nativePC_, copy))) break;
                writeFileInPlace(join(nativePC_, copy), data);
            }
        } else {
            writeFile(join(nativePC_, rel), data);
        }
    };
    writeRes(tex.normal, ".tex", buildTex(Image::solid(4, 4, 128, 128, 0, 255), TEX_R8G8B8A8_UNORM, false));
    writeRes(tex.rmt, ".tex", buildTex(Image::solid(4, 4, 200, 0, 0, 0), TEX_R8G8B8A8_UNORM, false));
    writeRes(tex.white, ".tex", buildTex(Image::solid(4, 4, 255, 255, 255, 255), TEX_R8G8B8A8_UNORM, false));
    writeRes(tex.black, ".tex", buildTex(Image::solid(4, 4, 0, 0, 0, 255), TEX_R8G8B8A8_UNORM, false));
    {
        // textures are independent: encode them on all CPU cores
        say("Converting " + std::to_string(vrm.materials.size()) + " textures...");
        std::vector<std::vector<uint8_t>> encoded(vrm.materials.size());
        std::atomic<size_t> next{0};
        std::vector<std::thread> pool;
        unsigned workers = std::max(1u, std::min(std::thread::hardware_concurrency(), 8u));
        for (unsigned t = 0; t < workers; ++t)
            pool.emplace_back([&] {
                for (size_t i; (i = next++) < vrm.materials.size();)
                    encoded[i] = buildTex(bakeAlbedo(vrm, vrm.materials[i], s.maxTextureSize), TEX_BC1_UNORM_SRGB, true);
            });
        for (auto& t : pool) t.join();
        for (size_t i = 0; i < vrm.materials.size(); ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "m%02d_bml", int(i));
            tex.albedo[int(i)] = texDirRes + name;
            writeRes(tex.albedo[int(i)], ".tex", encoded[i]);
        }
    }

    bool any = false;
    for (char g : {'f', 'm'}) {
        if ((g == 'f' && !s.applyToFemale) || (g == 'm' && !s.applyToMale)) continue;
        auto& gs = sets[g];
        // choose a complete armour set as template
        std::string chosen;
        for (const char* pref : {"pl001_0000", "pl002_0000", "pl003_0000", "pl000_0000"})
            if (gs.count(pref) && gs[pref].size() == size_t(kSlotCount)) { chosen = pref; break; }
        if (chosen.empty())
            for (auto& kv : gs)
                if (kv.second.size() == size_t(kSlotCount)) { chosen = kv.first; break; }
        if (chosen.empty()) {
            say(std::string("No complete ") + (g == 'f' ? "female" : "male") + " armour set found, skipping.");
            continue;
        }
        say(std::string("Using ") + (g == 'f' ? "female" : "male") + " armour " + chosen + " as skeleton template");
        auto pick = [&](int slot, const char* ext) -> std::string {
            const auto& v = gs[chosen][slot];
            std::string best;
            for (auto& p : v)
                if (endsWith(p, ext)) {
                    if (best.empty() || (p.find(slotFolder(Slot(slot))) != std::string::npos && best.find(slotFolder(Slot(slot))) == std::string::npos))
                        best = p;
                }
            return best;
        };
        std::vector<Mod3File> templates;
        std::vector<int> order = {int(Slot::Body), int(Slot::Helm), int(Slot::Arm), int(Slot::Waist), int(Slot::Leg)};
        TemplateMaterial best;
        for (int slot : order) {
            std::string m3 = pick(slot, ".mod3");
            if (m3.empty()) continue;
            Mod3File mf = Mod3File::parse(src.read(m3));
            std::string mr = pick(slot, ".mrl3");
            if (!mr.empty()) {
                Mrl3File rf = Mrl3File::parse(src.read(mr));
                // how are material names hashed? compare the template's own names
                int full = 0, masked = 0;
                for (auto& n : mf.materials)
                    for (auto& mat : rf.materials) {
                        if (mat.h.materialNameHash == jamcrc(n)) ++full;
                        if (mat.h.materialNameHash == (jamcrc(n) & 0x7fffffff)) ++masked;
                    }
                for (auto& mat : rf.materials) {
                    int sc = scoreMaterial(mat);
                    if (sc > best.score) {
                        best.score = sc;
                        best.file = rf;
                        best.material = mat;
                        best.maskedHash = masked > full;
                    }
                }
            }
            templates.push_back(std::move(mf));
        }
        if (templates.empty() || best.score <= -100) {
            say("Template files could not be read for this gender, skipping.");
            continue;
        }
        HunterSkeleton hs = buildHunterSkeleton(templates);
        RetargetOptions ro;
        ro.conform = s.conform;
        ro.dropTransparent = s.dropTransparent;
        say("Fitting the model to the hunter skeleton...");
        RetargetResult rr = retargetToHunter(vrm, hs, ro);
        matchGameConventions(rr.meshes, hs.frontFaceSign, hs.bitangentSign);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Scaled %.1fx (VRM %.2f m ankle-to-head, hunter %.1f units), %d bones matched, %d hidden triangles removed%s",
                      rr.report.scale, rr.report.vrmHeight, rr.report.hunterHeight, rr.report.mappedBones,
                      rr.report.droppedTriangles, rr.report.mirroredAxes ? ", mirrored axes" : "");
        say(buf);
        for (auto& w : rr.report.warnings) say("warning: " + w);

        for (int slot = 0; slot < kSlotCount; ++slot) {
            std::vector<const OutMesh*> meshes;
            std::vector<std::pair<std::string, int>> mats;
            std::map<int, int> vrmToLocal;
            std::vector<std::string> names;
            for (auto& om : rr.meshes) {
                if (int(om.slot) != slot) continue;
                meshes.push_back(&om);
                if (!vrmToLocal.count(om.material)) {
                    char nm[32];
                    std::snprintf(nm, sizeof(nm), "vh_mat_%02d", om.material);
                    vrmToLocal[om.material] = int(names.size());
                    names.push_back(nm);
                    mats.push_back({nm, om.material});
                }
            }
            if (mats.empty()) mats.push_back({"vh_empty", -1});
            Mod3File mod = makeSlotMod3(hs, meshes, names, vrmToLocal);
            Mrl3File mrl = makeSlotMrl3(best, mats, tex, log);
            std::string base = modelDir + "\\" + std::string(1, g) + "\\" + slotFolder(Slot(slot));
            writeRes(base, ".mod3", mod.serialize());
            writeRes(base, ".mrl3", mrl.serialize());
        }
        // invisible model used to hide the hunter's own face and hair
        Mod3File empty = makeSlotMod3(hs, {}, {}, {});
        Mrl3File emptyMrl = makeSlotMrl3(best, {{"vh_empty", -1}}, tex, log);
        writeRes(modelDir + "\\" + std::string(1, g) + "\\empty", ".mod3", empty.serialize());
        writeRes(modelDir + "\\" + std::string(1, g) + "\\empty", ".mrl3", emptyMrl.serialize());
        any = true;
    }
    if (!any) throw std::runtime_error("No hunter template models were found in the game files.");
    std::string done = toNativeRel(modelDir) + "/done.txt";
    writeFile(join(nativePC_, done), std::vector<uint8_t>(doneKey.begin(), doneKey.end()));
    addToManifest(done);
    return true;
}

void ModManager::addToManifest(const std::string& rel) {
    std::lock_guard<std::mutex> lk(manifestMutex_);
    manifest_.insert(rel);
}

int ModManager::installLinks(const std::map<std::string, std::string>& links, ApplyResult& r,
                             const std::function<void(const std::string&)>& say) {
    // NTFS allows at most 1023 names per file; leave headroom and add copies of a master as needed.
    constexpr uint64_t kMaxLinks = 1000;
    std::map<std::string, std::vector<std::string>> byMaster;
    for (auto& [link, master] : links) byMaster[master].push_back(link);
    if (byMaster.empty()) return 0;

    // Hard links are what makes this fast and small. Without them (FAT32/exFAT drives) every
    // armour file would be a full copy: many gigabytes, so refuse instead of grinding away.
    {
        const std::string master = join(nativePC_, byMaster.begin()->first);
        const std::string probe = join(nativePC_, "vroidhunter/.linktest");
        removeFile(probe);
        std::error_code ec;
        fs::create_hard_link(u8(master), u8(probe), ec);
        removeFile(probe);
        if (ec)
            throw std::runtime_error("The drive with Monster Hunter World does not support hard links (" + ec.message() +
                                     "). The mod needs an NTFS formatted drive; FAT32/exFAT drives would need many "
                                     "gigabytes of copies.");
    }

    // After a complete install, every link listed in the manifest is known to be right (masters
    // never move and are rewritten in place), so only new links need work. After an interrupted
    // run or an older version, check each file instead.
    const std::string completeMarker = join(dataDir_, "links_complete.txt");
    const bool trustManifest = exists(completeMarker);
    removeFile(completeMarker);

    std::vector<const std::pair<const std::string, std::vector<std::string>>*> groups;
    for (auto& g : byMaster) groups.push_back(&g);
    std::atomic<size_t> nextGroup{0};
    std::atomic<int> done{0}, failed{0}, conflicts{0}, relinked{0};
    std::mutex sayMutex;
    auto worker = [&] {
        for (size_t gi; (gi = nextGroup++) < groups.size();) {
            const std::string& masterRel = groups[gi]->first;
            const std::string ext = masterRel.substr(masterRel.size() - 5);
            const std::string base = masterRel.substr(0, masterRel.size() - 5);
            std::vector<std::string> targets = {masterRel};
            for (int k = 1; k < 64 && exists(join(nativePC_, base + "~" + std::to_string(k) + ext)); ++k)
                targets.push_back(base + "~" + std::to_string(k) + ext);
            size_t current = 0;
            uint64_t count = hardLinkCount(join(nativePC_, targets[0]));
            for (const std::string& link : groups[gi]->second) {
                const std::string abs = join(nativePC_, link);
                bool ours;
                {
                    std::lock_guard<std::mutex> lk(manifestMutex_);
                    ours = manifest_.count(link) != 0;
                }
                if (ours && trustManifest) {
                    ++done;
                    continue;
                }
                const bool present = exists(abs);
                if (present && !ours) {
                    ++conflicts; // belongs to another mod: never overwrite it
                    continue;
                }
                bool ok = false;
                if (present)
                    for (auto& t : targets)
                        if (sameFile(abs, join(nativePC_, t))) { ok = true; break; } // already correct: nothing to do
                if (ok) {
                    ++done;
                    continue;
                }
                try {
                    addToManifest(link); // record first so an interrupted run can still be cleaned up
                    while (count >= kMaxLinks) {
                        ++current;
                        if (current == targets.size()) {
                            std::string copy = base + "~" + std::to_string(current) + ext;
                            fs::copy_file(u8(join(nativePC_, targets[0])), u8(join(nativePC_, copy)), fs::copy_options::overwrite_existing);
                            addToManifest(copy);
                            targets.push_back(copy);
                        }
                        count = hardLinkCount(join(nativePC_, targets[current]));
                    }
                    fs::path l = u8(abs);
                    std::error_code ec;
                    fs::create_directories(l.parent_path(), ec);
                    fs::remove(l, ec);
                    ec.clear();
                    fs::create_hard_link(u8(join(nativePC_, targets[current])), l, ec);
                    if (ec) throw std::runtime_error(link + ": " + ec.message());
                    ++count;
                    ++done;
                    ++relinked;
                } catch (const std::exception& e) {
                    // usually a file the game currently has open; it is retried on the next apply
                    {
                        std::lock_guard<std::mutex> lk(manifestMutex_);
                        if (!exists(abs)) manifest_.erase(link);
                    }
                    if (failed++ < 5) {
                        std::lock_guard<std::mutex> lk(sayMutex);
                        say(std::string("warning: ") + e.what());
                    }
                }
            }
            std::lock_guard<std::mutex> lk(manifestMutex_);
            saveManifest(); // progress survives the game being closed mid-way
        }
    };
    unsigned workers = std::max(1u, std::min(std::thread::hardware_concurrency(), 8u));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < workers; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    r.skippedConflicts = conflicts;
    if (!failed) {
        std::string ok = "ok";
        writeFile(completeMarker, std::vector<uint8_t>(ok.begin(), ok.end()));
    }
    if (failed) say(std::to_string(failed.load()) + " files could not be replaced (in use?). Apply again or restart the game.");
    say(std::to_string(relinked.load()) + " links created, " + std::to_string(done - relinked) + " already up to date");
    return done;
}

ApplyResult ModManager::apply(const Settings& s, const std::function<void(const std::string&)>& progress) {
    if (!s.enabled) return removeAll();
    ApplyResult r;
    auto say = [&](const std::string& m) {
        r.log.push_back(m);
        if (progress) progress(m);
    };
    try {
        if (s.vrmPath.empty() || !exists(s.vrmPath)) throw std::runtime_error("Choose a .vrm file first.");
        say("Opening game files...");
        std::unique_ptr<GameSource> src = sourceFactory_();
        std::vector<std::string> files = src->list();
        if (files.empty()) throw std::runtime_error("No hunter model files were found in the game data.");

        const std::string key = std::string(kModVersion) + "|" + s.modelKey() + "|" + (s.applyToFemale ? "f" : "") + (s.applyToMale ? "m" : "");
        const std::string modelDir = "vroidhunter\\model";
        const std::string texTag = hex8(jamcrc(key));
        const std::string doneRel = toNativeRel(modelDir) + "/done.txt";
        auto seconds = [](std::chrono::steady_clock::time_point t0) {
            char b[32];
            std::snprintf(b, sizeof(b), "%.1fs", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            return std::string(b);
        };

        std::set<std::string> previous = manifest_;
        std::string doneContent;
        try {
            auto d = readFile(join(nativePC_, doneRel));
            doneContent.assign(d.begin(), d.end());
        } catch (...) {
        }
        if (doneContent != key) {
            auto t0 = std::chrono::steady_clock::now();
            buildModel(s, *src, files, modelDir, texTag, key, r.log, progress);
            say("Model converted in " + seconds(t0));
        } else {
            say("Model already converted, reusing it.");
        }

        // desired files: everything in our model folder (except textures of older models) plus the links
        std::set<std::string> desired;
        std::string prefix = toNativeRel(modelDir) + "/";
        const std::string texPrefix = prefix + "tex_";
        const std::string currentTex = prefix + "tex_" + texTag + "/";
        for (auto& p : manifest_)
            if (startsWith(p, prefix.c_str()) && (!startsWith(p, texPrefix.c_str()) || startsWith(p, currentTex.c_str())))
                desired.insert(p);

        std::map<std::string, std::string> links; // link (relative) -> master (relative)
        for (char g : {'f', 'm'}) {
            if ((g == 'f' && !s.applyToFemale) || (g == 'm' && !s.applyToMale)) continue;
            std::string gdir = prefix + std::string(1, g) + "/";
            if (!exists(join(nativePC_, gdir + "body.mod3"))) continue;
            for (auto& p : files) {
                ArmorPath ap;
                if (parseArmorPath(p, ap) && ap.gender == g) {
                    if (s.showArmor[ap.slot]) continue;
                    std::string master = gdir + slotFolder(Slot(ap.slot)) + (endsWith(p, ".mod3") ? ".mod3" : ".mrl3");
                    links[toNativeRel(p)] = master;
                } else if (!s.showArmor[int(Slot::Helm)] && isFaceOrHair(p, g)) {
                    links[toNativeRel(p)] = gdir + (endsWith(p, ".mod3") ? "empty.mod3" : "empty.mrl3");
                }
            }
        }

        // remove stale files from earlier runs (old models, links no longer wanted)
        for (auto& p : previous) {
            if (desired.count(p) || links.count(p)) continue;
            removeFile(join(nativePC_, p));
            manifest_.erase(p);
        }
        // remove empty old model folders
        {
            std::error_code ec;
            fs::path root = u8(join(nativePC_, "vroidhunter"));
            if (fs::exists(root, ec))
                for (auto& e : fs::directory_iterator(root, ec))
                    if (e.is_directory() && toU8(e.path().filename()) != "model") fs::remove_all(e.path(), ec); // older versions
            fs::path model = u8(join(nativePC_, toNativeRel(modelDir)));
            if (fs::exists(model, ec))
                for (auto& e : fs::directory_iterator(model, ec)) {
                    std::string n = toU8(e.path().filename());
                    if (e.is_directory() && n.rfind("tex_", 0) == 0 && n != "tex_" + texTag) fs::remove_all(e.path(), ec);
                }
        }

        say("Installing model files...");
        auto t0 = std::chrono::steady_clock::now();
        const int n = installLinks(links, r, say);
        say("Installed " + std::to_string(n) + " files in " + seconds(t0));
        r.linkedFiles = n;
        saveManifest();
        if (r.skippedConflicts)
            say(std::to_string(r.skippedConflicts) + " files were left alone because another mod already provides them.");
        r.ok = true;
        r.message = "Done! Change equipment, visit your room or travel to apply it in game (" + std::to_string(n) + " files installed).";
        say(r.message);
        // human readable report
        std::string rep;
        for (auto& l : r.log) rep += l + "\n";
        writeFile(join(dataDir_, "last_build_report.txt"), std::vector<uint8_t>(rep.begin(), rep.end()));
    } catch (const std::exception& e) {
        saveManifest();
        r.ok = false;
        r.message = e.what();
        r.log.push_back(std::string("error: ") + e.what());
    }
    return r;
}

} // namespace vh
