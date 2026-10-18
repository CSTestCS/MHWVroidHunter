// File system helpers. All paths are UTF-8 strings; on Windows they are converted to wide
// paths so that non-ASCII folder names (common for game libraries and VRM files) work.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace vh {

std::filesystem::path u8(const std::string& utf8);
std::string toU8(const std::filesystem::path& p);

std::vector<uint8_t> readFile(const std::string& path);
// Writes to a temporary file and renames it into place.
void writeFile(const std::string& path, const std::vector<uint8_t>& data);
bool exists(const std::string& path);
void makeDirs(const std::string& path);
bool removeFile(const std::string& path);
uint64_t fileSize(const std::string& path);
int64_t fileTime(const std::string& path);

// Creates `linkPath` as a hard link to `target` (falls back to copying when links are not
// possible, e.g. FAT32 drives or the 1023 link limit). Returns true if a real link was made.
bool linkOrCopy(const std::string& target, const std::string& linkPath);

// Joins with a forward slash; std::filesystem normalises separators on Windows.
std::string join(const std::string& a, const std::string& b);

// Random access reader for very large files (game archives).
class RandomFile {
public:
    explicit RandomFile(const std::string& path);
    void read(uint64_t offset, void* out, size_t n);
    uint64_t size() const { return size_; }

private:
    std::ifstream f_;
    uint64_t size_ = 0;
};

} // namespace vh
