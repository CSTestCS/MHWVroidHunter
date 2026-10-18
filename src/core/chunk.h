// Reader for Monster Hunter World chunk archives (chunkG*.bin).
//
// An archive is a list of independently Oodle-compressed 256 KiB blocks. Iceborne archives are
// additionally XOR-encrypted per block with one of 16 keys chosen by a fixed key sequence. The
// decompressed blocks form one virtual stream that starts with the file table.
// Format documented by WorldChunkTool (MHVuze) and MHWNoChunk.
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "fsutil.h"

namespace vh {

// Decompresses one block. Returns the number of bytes written or -1.
using Decompressor = std::function<int(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCapacity)>;

// Open-source decoder (ooz). On Windows, `gameDir` is also searched for oo2core_*_win64.dll,
// which is preferred when present.
Decompressor makeDecompressor(const std::string& gameDir);

// Key used to decrypt block `index` of an Iceborne archive.
const uint8_t* chunkKeyForBlock(size_t index);
void decryptBlock(uint8_t* data, size_t n, size_t blockIndex);

struct ArchiveFile {
    std::string path; // lower case, backslash separated, no leading backslash
    uint64_t offset = 0;
    uint64_t size = 0;
};

class ChunkArchive {
public:
    ChunkArchive(const std::string& path, bool encrypted, Decompressor dec);
    // Parses the file table. The callback may return false to skip storing an entry.
    std::vector<ArchiveFile> listFiles(const std::function<bool(const std::string&)>& filter = nullptr);
    std::vector<uint8_t> read(uint64_t offset, uint64_t size);
    size_t blockCount() const { return blocks_.size(); }

    static constexpr uint32_t kBlockSize = 0x40000;

private:
    struct Block {
        uint64_t offset;
        uint64_t size; // 0 = stored uncompressed
    };
    const std::vector<uint8_t>& block(size_t i);
    void readStream(uint64_t pos, void* out, size_t n);

    RandomFile file_;
    bool encrypted_;
    Decompressor dec_;
    std::vector<Block> blocks_;
    std::list<size_t> lru_;
    std::unordered_map<size_t, std::pair<std::vector<uint8_t>, std::list<size_t>::iterator>> cache_;
};

// Normalises a game resource path: lower case, backslashes, no leading separator.
std::string normalizeGamePath(const std::string& p);

// All chunk archives of an installation, later archives override earlier ones.
class GameArchives {
public:
    explicit GameArchives(const std::string& gameDir);
    // Lists chunk archive files in load order.
    static std::vector<std::string> findArchives(const std::string& gameDir);

    struct Location {
        int archive;
        uint64_t offset;
        uint64_t size;
    };
    // Indexes all files whose path passes the filter.
    void index(const std::function<bool(const std::string&)>& filter,
               const std::function<void(const std::string&)>& progress = nullptr);
    const std::map<std::string, Location>& files() const { return files_; }
    bool has(const std::string& path) const { return files_.count(normalizeGamePath(path)) != 0; }
    std::vector<uint8_t> read(const std::string& path);
    // Fingerprint of the archive set (names, sizes, times) for cache invalidation.
    std::string fingerprint() const;
    void setFiles(std::map<std::string, Location> f) { files_ = std::move(f); }

private:
    std::string gameDir_;
    std::vector<std::string> archivePaths_;
    std::vector<std::unique_ptr<ChunkArchive>> archives_;
    std::map<std::string, Location> files_;
    Decompressor dec_;
    ChunkArchive& archive(int i);
};

} // namespace vh
