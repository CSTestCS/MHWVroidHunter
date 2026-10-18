#include "fsutil.h"

#include <chrono>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace vh {

fs::path u8(const std::string& utf8) {
#if defined(_WIN32)
    return fs::u8path(utf8);
#else
    return fs::path(utf8);
#endif
}

std::string toU8(const fs::path& p) {
#if defined(_WIN32)
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
#else
    return p.string();
#endif
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(u8(path), std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size_t(n > 0 ? n : 0));
    if (n > 0 && !f.read(reinterpret_cast<char*>(data.data()), n)) throw std::runtime_error("Cannot read file: " + path);
    return data;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    fs::path p = u8(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    fs::path tmp = p;
    tmp += ".vhtmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot write file: " + path);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) throw std::runtime_error("Cannot write file (disk full?): " + path);
    }
    std::error_code ec;
    fs::remove(p, ec); // rename over an existing hard link must not modify the other links
    fs::rename(tmp, p, ec);
    if (ec) throw std::runtime_error("Cannot replace file: " + path + " (" + ec.message() + ")");
}

void writeFileInPlace(const std::string& path, const std::vector<uint8_t>& data) {
    fs::path p = u8(path);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        writeFile(path, data);
        return;
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc); // truncates the same file (keeps its links)
    if (!f) throw std::runtime_error("Cannot write file (in use?): " + path);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!f) throw std::runtime_error("Cannot write file (disk full?): " + path);
}

bool sameFile(const std::string& a, const std::string& b) {
    std::error_code ec;
    bool same = fs::equivalent(u8(a), u8(b), ec);
    return !ec && same;
}

uint64_t hardLinkCount(const std::string& path) {
    std::error_code ec;
    auto n = fs::hard_link_count(u8(path), ec);
    return ec ? 0 : uint64_t(n);
}

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(u8(path), ec);
}

void makeDirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(u8(path), ec);
    if (ec) throw std::runtime_error("Cannot create folder: " + path + " (" + ec.message() + ")");
}

bool removeFile(const std::string& path) {
    std::error_code ec;
    return fs::remove(u8(path), ec);
}

uint64_t fileSize(const std::string& path) {
    std::error_code ec;
    auto s = fs::file_size(u8(path), ec);
    return ec ? 0 : uint64_t(s);
}

int64_t fileTime(const std::string& path) {
    std::error_code ec;
    auto t = fs::last_write_time(u8(path), ec);
    if (ec) return 0;
    return int64_t(t.time_since_epoch().count());
}

bool linkOrCopy(const std::string& target, const std::string& linkPath) {
    fs::path l = u8(linkPath);
    std::error_code ec;
    if (l.has_parent_path()) fs::create_directories(l.parent_path(), ec);
    fs::remove(l, ec);
    ec.clear();
    fs::create_hard_link(u8(target), l, ec);
    if (!ec) return true;
    ec.clear();
    fs::copy_file(u8(target), l, fs::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error("Cannot create file: " + linkPath + " (" + ec.message() + ")");
    return false;
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

RandomFile::RandomFile(const std::string& path) : f_(u8(path), std::ios::binary) {
    if (!f_) throw std::runtime_error("Cannot open game archive: " + path);
    f_.seekg(0, std::ios::end);
    size_ = uint64_t(f_.tellg());
}

void RandomFile::read(uint64_t offset, void* out, size_t n) {
    if (offset + n > size_) throw std::runtime_error("read past end of game archive");
    f_.clear();
    f_.seekg(std::streamoff(offset));
    if (!f_.read(static_cast<char*>(out), std::streamsize(n))) throw std::runtime_error("game archive read failed");
}

} // namespace vh
