// Little-endian binary reader/writer helpers.
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace vh {

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : data_(v.data()), size_(v.size()) {}

    size_t tell() const { return pos_; }
    size_t size() const { return size_; }
    void seek(size_t p) {
        if (p > size_) throw FormatError("seek past end of file");
        pos_ = p;
    }
    void skip(size_t n) { seek(pos_ + n); }
    void read(void* out, size_t n) {
        if (pos_ + n > size_ || pos_ + n < pos_) throw FormatError("unexpected end of file");
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
    }
    template <class T> T get() {
        T v;
        read(&v, sizeof(T));
        return v;
    }
    std::vector<uint8_t> bytes(size_t n) {
        std::vector<uint8_t> v(n);
        if (n) read(v.data(), n);
        return v;
    }
    std::string fixedString(size_t n) {
        std::vector<uint8_t> b = bytes(n);
        size_t len = 0;
        while (len < n && b[len] != 0) ++len;
        return std::string(reinterpret_cast<const char*>(b.data()), len);
    }
    const uint8_t* data() const { return data_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

class ByteWriter {
public:
    std::vector<uint8_t> buf;
    size_t tell() const { return buf.size(); }
    void write(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    template <class T> void put(const T& v) { write(&v, sizeof(T)); }
    void zeros(size_t n) { buf.insert(buf.end(), n, 0); }
    void padTo(size_t offset) {
        if (offset < buf.size()) throw std::logic_error("padTo moving backwards");
        zeros(offset - buf.size());
    }
    void align(size_t a) {
        while (buf.size() % a) buf.push_back(0);
    }
    void fixedString(const std::string& s, size_t n) {
        if (s.size() >= n) throw FormatError("string too long for fixed field: " + s);
        write(s.data(), s.size());
        zeros(n - s.size());
    }
    template <class T> void patch(size_t at, const T& v) { std::memcpy(buf.data() + at, &v, sizeof(T)); }
};

inline size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// CRC-32/JAMCRC (bitwise NOT of the standard zlib CRC-32), used by MT Framework for name hashes.
uint32_t jamcrc(const void* data, size_t len);
inline uint32_t jamcrc(const std::string& s) { return jamcrc(s.data(), s.size()); }

} // namespace vh
