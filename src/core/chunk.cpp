#include "chunk.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <tuple>

#if defined(_WIN32)
#include <windows.h>
#endif

// 16 block keys and the key order, from WorldChunkTool (MHVuze; key order found by legendff).
// The order table is stored packed, two 4-bit entries per byte, and embedded at build time.
#if defined(_WIN32)
#define VH_RODATA ".section .rdata,\"dr\"\n"
#else
#define VH_RODATA ".section .rodata\n"
#endif
__asm__(VH_RODATA
        ".balign 16\n"
        ".global vh_keyseq_data\n"
        "vh_keyseq_data:\n"
        ".incbin \"" VH_KEYSEQ_PATH "\"\n"
        ".global vh_keyseq_data_end\n"
        "vh_keyseq_data_end:\n"
        ".byte 0\n"
        ".text\n");
extern "C" const uint8_t vh_keyseq_data[];
extern "C" const uint8_t vh_keyseq_data_end[];

int Kraken_DecompressBounded(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len);

namespace vh {

namespace {

const uint8_t kKeys[16][16] = {
    {0xac, 0x76, 0xcb, 0x97, 0xec, 0x75, 0x00, 0x13, 0x3a, 0x81, 0x03, 0x8e, 0x7a, 0x82, 0xc8, 0x0a},
    {0x6b, 0xb5, 0xc9, 0x56, 0xe4, 0x4d, 0x00, 0xbc, 0x30, 0x52, 0x33, 0xcf, 0xbf, 0xaa, 0xfa, 0x25},
    {0x8f, 0x02, 0x1d, 0xcc, 0xb0, 0xf2, 0x78, 0x72, 0x06, 0xfb, 0xde, 0xe2, 0x39, 0x0b, 0xbb, 0x5c},
    {0xda, 0x5c, 0x1e, 0x53, 0x1d, 0x83, 0x59, 0x15, 0x78, 0x75, 0xbd, 0x63, 0xbf, 0xaa, 0xfa, 0x25},
    {0x1f, 0x6d, 0x31, 0xc8, 0x83, 0xdd, 0x71, 0x6d, 0x7e, 0x8f, 0x59, 0x8c, 0xe2, 0x3f, 0x19, 0x29},
    {0x4b, 0xb0, 0xde, 0x04, 0xe4, 0xe0, 0x85, 0x69, 0x80, 0xcc, 0xb2, 0x94, 0x2f, 0x9c, 0xe9, 0xf9},
    {0x8b, 0xce, 0x54, 0xdc, 0x4c, 0x11, 0x13, 0x9a, 0x78, 0x75, 0xbd, 0x63, 0xbf, 0xaa, 0xfa, 0x25},
    {0xec, 0x13, 0x34, 0x59, 0x66, 0xce, 0x73, 0x12, 0x44, 0x00, 0x89, 0xa2, 0xce, 0xdd, 0xce, 0xe9},
    {0xe4, 0x66, 0x2c, 0x70, 0x9c, 0x75, 0x3a, 0x03, 0x9a, 0x2c, 0x0f, 0x5a, 0xe2, 0x3f, 0x19, 0x29},
    {0xa4, 0x92, 0xfc, 0x90, 0x33, 0x94, 0x9c, 0x15, 0xa0, 0x33, 0xac, 0x22, 0x37, 0x35, 0xcc, 0xa7},
    {0x25, 0x09, 0x9c, 0x1f, 0x91, 0x1a, 0x26, 0xe5, 0xce, 0x91, 0x72, 0xf0, 0x7a, 0x82, 0xc8, 0x0a},
    {0xd1, 0xd2, 0x9d, 0x74, 0x46, 0xd4, 0xfd, 0xf1, 0xa0, 0x33, 0xac, 0x22, 0x37, 0x35, 0xcc, 0xa7},
    {0x7e, 0xb2, 0x68, 0x37, 0x3b, 0x5d, 0x36, 0x1e, 0xd6, 0xd3, 0x13, 0xe2, 0x93, 0x3c, 0x4d, 0xcb},
    {0xa1, 0xc7, 0xd2, 0xea, 0x66, 0x18, 0x95, 0xac, 0x78, 0x75, 0xbd, 0x63, 0xbf, 0xaa, 0xfa, 0x25},
    {0x82, 0xa4, 0x3b, 0x21, 0x08, 0x79, 0x7c, 0x6a, 0x44, 0x00, 0x89, 0xa2, 0xce, 0xdd, 0xce, 0xe9},
    {0x41, 0xd0, 0x55, 0xb3, 0xdd, 0x60, 0x15, 0x16, 0x7e, 0x8f, 0x59, 0x8c, 0xe2, 0x3f, 0x19, 0x29},
};

uint64_t readLE(const uint8_t* p, int n) {
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

#if defined(_WIN32)
typedef int(WINAPI* OodleDecompressFn)(const void*, int64_t, void*, int64_t, int, int, int, void*, int64_t, void*, void*,
                                       void*, int64_t, int);
#endif

} // namespace

const uint8_t* chunkKeyForBlock(size_t index) {
    size_t entries = size_t(vh_keyseq_data_end - vh_keyseq_data) * 2;
    if (index >= entries) throw std::runtime_error("game archive is larger than the known key sequence");
    uint8_t b = vh_keyseq_data[index / 2];
    uint8_t k = (index & 1) ? (b >> 4) : (b & 0xF);
    return kKeys[k];
}

void decryptBlock(uint8_t* data, size_t n, size_t blockIndex) {
    const uint8_t* key = chunkKeyForBlock(blockIndex);
    for (size_t i = 0; i < n; ++i) data[i] ^= key[i & 15];
}

Decompressor makeDecompressor(const std::string& gameDir) {
#if defined(_WIN32)
    for (const char* name : {"oo2core_8_win64.dll", "oo2core_9_win64.dll"}) {
        std::wstring p = u8(join(gameDir, name)).wstring();
        HMODULE h = GetModuleHandleW(u8(name).wstring().c_str());
        if (!h && exists(join(gameDir, name))) h = LoadLibraryW(p.c_str());
        if (!h) continue;
        auto fn = reinterpret_cast<OodleDecompressFn>(reinterpret_cast<void*>(GetProcAddress(h, "OodleLZ_Decompress")));
        if (!fn) continue;
        return [fn](const uint8_t* src, size_t srcLen, uint8_t* dst, size_t cap) {
            return fn(src, int64_t(srcLen), dst, int64_t(cap), 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3);
        };
    }
#else
    (void)gameDir;
#endif
    return [](const uint8_t* src, size_t srcLen, uint8_t* dst, size_t cap) {
        // ooz may read and write a few bytes past the buffers; use padded copies.
        std::vector<uint8_t> in(srcLen + 64, 0), out(cap + 64, 0);
        std::memcpy(in.data(), src, srcLen);
        int n = Kraken_DecompressBounded(in.data(), srcLen, out.data(), cap);
        if (n > 0) std::memcpy(dst, out.data(), size_t(n));
        return n;
    };
}

ChunkArchive::ChunkArchive(const std::string& path, bool encrypted, Decompressor dec)
    : file_(path), encrypted_(encrypted), dec_(std::move(dec)) {
    uint8_t head[8];
    file_.read(0, head, 8);
    uint32_t count;
    std::memcpy(&count, head + 4, 4);
    if (count == 0 || uint64_t(count) * 8 + 8 > file_.size()) throw std::runtime_error("not a chunk archive: " + path);
    std::vector<uint8_t> table(size_t(count) * 8);
    file_.read(8, table.data(), table.size());
    blocks_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = &table[size_t(i) * 8];
        // Size field: 3 bytes with a quirky encoding, reproduced from WorldChunkTool.
        uint64_t v = uint64_t(e[0] >> 4) | (uint64_t(e[1]) << 8) | (uint64_t(e[2]) << 16);
        blocks_[i].size = (v >> 4) + (v & 0xF);
        blocks_[i].offset = readLE(e + 3, 5);
    }
}

const std::vector<uint8_t>& ChunkArchive::block(size_t i) {
    auto it = cache_.find(i);
    if (it != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.second);
        return it->second.first;
    }
    if (i >= blocks_.size()) throw std::runtime_error("archive block out of range");
    std::vector<uint8_t> out(kBlockSize, 0);
    const Block& b = blocks_[i];
    if (b.size != 0) {
        std::vector<uint8_t> comp(size_t(b.size));
        file_.read(b.offset, comp.data(), comp.size());
        int n = dec_(comp.data(), comp.size(), out.data(), kBlockSize);
        if (n <= 0) throw std::runtime_error("could not decompress game archive block " + std::to_string(i));
        out.resize(size_t(n));
    } else {
        uint64_t avail = std::min<uint64_t>(kBlockSize, file_.size() - std::min(file_.size(), b.offset));
        out.resize(size_t(avail));
        file_.read(b.offset, out.data(), out.size());
    }
    if (encrypted_) decryptBlock(out.data(), out.size(), i);
    if (cache_.size() >= 48) {
        size_t victim = lru_.back();
        lru_.pop_back();
        cache_.erase(victim);
    }
    lru_.push_front(i);
    auto& slot = cache_[i];
    slot.first = std::move(out);
    slot.second = lru_.begin();
    return slot.first;
}

void ChunkArchive::readStream(uint64_t pos, void* outp, size_t n) {
    uint8_t* out = static_cast<uint8_t*>(outp);
    while (n) {
        size_t bi = size_t(pos / kBlockSize);
        size_t off = size_t(pos % kBlockSize);
        const std::vector<uint8_t>& b = block(bi);
        if (off >= b.size()) throw std::runtime_error("read past end of archive data");
        size_t take = std::min(n, b.size() - off);
        std::memcpy(out, b.data() + off, take);
        out += take;
        n -= take;
        pos += take;
    }
}

bool ChunkArchive::looksValid() {
    try {
        uint8_t hdr[8];
        readStream(0x0C, hdr, 8);
        int32_t parents, children;
        std::memcpy(&parents, hdr, 4);
        std::memcpy(&children, hdr + 4, 4);
        if (parents <= 0 || parents > 100000 || children < 0) return false;
        uint8_t first[0x3C];
        readStream(0x100, first, sizeof(first));
        // the first directory name is plain text (e.g. "\\")
        for (uint8_t c : first) {
            if (c == 0) break;
            if (c < 0x20 || c > 0x7e) return false;
        }
        return first[0] != 0;
    } catch (...) {
        return false;
    }
}

std::vector<uint8_t> ChunkArchive::read(uint64_t offset, uint64_t size) {
    if (size > (1ull << 31)) throw std::runtime_error("file too large");
    std::vector<uint8_t> v(static_cast<size_t>(size));
    if (size) readStream(offset, v.data(), v.size());
    return v;
}

std::string normalizeGamePath(const std::string& p) {
    std::string s;
    s.reserve(p.size());
    for (char c : p) {
        if (c == '/') c = '\\';
        s.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    }
    size_t a = 0;
    while (a < s.size() && s[a] == '\\') ++a;
    return s.substr(a);
}

std::vector<ArchiveFile> ChunkArchive::listFiles(const std::function<bool(const std::string&)>& filter) {
    std::vector<ArchiveFile> out;
    uint8_t hdr[8];
    readStream(0x0C, hdr, 8);
    int32_t parents;
    std::memcpy(&parents, hdr, 4);
    if (parents < 0 || parents > 1000000) throw std::runtime_error("bad archive file table");
    uint64_t pos = 0x100;
    for (int32_t p = 0; p < parents; ++p) {
        uint8_t pe[0x58];
        readStream(pos, pe, sizeof(pe));
        pos += sizeof(pe);
        int32_t children;
        std::memcpy(&children, pe + 0x3C + 8 + 8 + 4, 4);
        if (children < 0) throw std::runtime_error("bad archive directory entry");
        for (int32_t c = 0; c < children; ++c) {
            uint8_t ce[0xB8];
            readStream(pos, ce, sizeof(ce));
            pos += sizeof(ce);
            int64_t size, offset;
            int32_t type;
            std::memcpy(&size, ce + 0xA0, 8);
            std::memcpy(&offset, ce + 0xA8, 8);
            std::memcpy(&type, ce + 0xB0, 4);
            if (type != 0 && type != 2) continue; // directory
            size_t maxName = type == 2 ? 0x50 : 0xA0;
            size_t len = 0;
            while (len < maxName && ce[len] != 0) ++len;
            std::string name = normalizeGamePath(std::string(reinterpret_cast<const char*>(ce), len));
            if (filter && !filter(name)) continue;
            out.push_back(ArchiveFile{name, uint64_t(offset), uint64_t(size)});
        }
    }
    return out;
}

namespace {

std::string lowerCase(std::string s) {
    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// "chunkG12.bin" -> 12, "chunk3.bin" -> 3, anything else -> -1
int chunkNumber(const std::string& lowerName) {
    if (lowerName.rfind("chunk", 0) != 0 || lowerName.size() < 10 || lowerName.substr(lowerName.size() - 4) != ".bin") return -1;
    std::string num = lowerName.substr(5, lowerName.size() - 9);
    if (!num.empty() && num[0] == 'g') num = num.substr(1);
    if (num.empty() || !std::all_of(num.begin(), num.end(), ::isdigit)) return -1;
    return std::atoi(num.c_str());
}

} // namespace

std::vector<std::string> GameArchives::searchFolders(const std::string& gameDir) {
    // The Steam version keeps its archives in <game>/chunk (and the high resolution texture
    // pack in <game>/chunkHiPC); older builds had them next to the exe.
    std::vector<std::string> dirs = {gameDir};
    std::vector<std::string> sub;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(u8(gameDir), ec)) {
        if (!e.is_directory(ec)) continue;
        std::string n = lowerCase(toU8(e.path().filename()));
        if (n.rfind("chunk", 0) == 0) sub.push_back(toU8(e.path()));
    }
    // "chunk" first, then the others (e.g. chunkHiPC) so they override it
    std::sort(sub.begin(), sub.end(), [](const std::string& x, const std::string& y) {
        std::string a = lowerCase(toU8(u8(x).filename())), b = lowerCase(toU8(u8(y).filename()));
        if ((a == "chunk") != (b == "chunk")) return a == "chunk";
        return a < b;
    });
    dirs.insert(dirs.end(), sub.begin(), sub.end());
    return dirs;
}

std::vector<std::string> GameArchives::findArchives(const std::string& gameDir) {
    std::vector<std::tuple<int, int, std::string>> found; // folder order, number, path
    std::vector<std::string> dirs = searchFolders(gameDir);
    for (size_t d = 0; d < dirs.size(); ++d) {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(u8(dirs[d]), ec)) {
            if (!e.is_regular_file(ec)) continue;
            int n = chunkNumber(lowerCase(toU8(e.path().filename())));
            if (n >= 0) found.emplace_back(int(d), n, toU8(e.path()));
        }
    }
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    for (auto& f : found) out.push_back(std::get<2>(f));
    return out;
}

GameArchives::GameArchives(const std::string& gameDir) : gameDir_(gameDir) {
    archivePaths_ = findArchives(gameDir);
    if (archivePaths_.empty()) {
        std::string where;
        for (auto& d : searchFolders(gameDir)) where += "\n  " + d;
        throw std::runtime_error("No chunk*.bin game archives were found. Searched:" + where +
                                 "\nIs this the Monster Hunter World folder?");
    }
    archives_.resize(archivePaths_.size());
    dec_ = makeDecompressor(gameDir);
}

ChunkArchive& GameArchives::archive(int i) {
    auto& a = archives_.at(size_t(i));
    if (!a) {
        const std::string& path = archivePaths_[size_t(i)];
        // Iceborne archives (chunkG*) are encrypted, older ones are not; try the other mode if the
        // file table does not look right.
        bool guess = lowerCase(toU8(u8(path).filename())).rfind("chunkg", 0) == 0;
        std::string firstError;
        for (bool encrypted : {guess, !guess}) {
            try {
                auto c = std::make_unique<ChunkArchive>(path, encrypted, dec_);
                if (c->looksValid()) {
                    a = std::move(c);
                    break;
                }
                if (firstError.empty()) firstError = "unrecognised file table";
            } catch (const std::exception& e) {
                if (firstError.empty()) firstError = e.what();
            }
        }
        if (!a) throw std::runtime_error("Could not read " + path + ": " + firstError);
    }
    return *a;
}

void GameArchives::index(const std::function<bool(const std::string&)>& filter,
                         const std::function<void(const std::string&)>& progress) {
    files_.clear();
    std::string errors;
    int ok = 0;
    for (size_t i = 0; i < archivePaths_.size(); ++i) {
        const std::string name = toU8(u8(archivePaths_[i]).parent_path().filename()) + "/" + toU8(u8(archivePaths_[i]).filename());
        if (progress) progress("Reading game file list " + std::to_string(i + 1) + "/" + std::to_string(archivePaths_.size()) + " (" + name + ")");
        try {
            for (auto& f : archive(int(i)).listFiles(filter)) files_[f.path] = Location{int(i), f.offset, f.size};
            ++ok;
        } catch (const std::exception& e) {
            // one unreadable archive (e.g. an optional pack) must not stop everything
            errors += std::string("\n  ") + e.what();
            if (progress) progress("warning: skipped " + name + ": " + e.what());
        }
    }
    if (!ok) throw std::runtime_error("None of the game archives could be read:" + errors);
}

std::vector<uint8_t> GameArchives::read(const std::string& path) {
    auto it = files_.find(normalizeGamePath(path));
    if (it == files_.end()) throw std::runtime_error("game file not found: " + path);
    return archive(it->second.archive).read(it->second.offset, it->second.size);
}

std::string GameArchives::fingerprint() const {
    std::string s;
    for (auto& p : archivePaths_) s += p + ":" + std::to_string(fileSize(p)) + ":" + std::to_string(fileTime(p)) + ";";
    return s;
}

} // namespace vh
