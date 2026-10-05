#pragma once

// Little helpers for seek's on-disk index format. Integers are little-endian; most numbers are
// written as LEB128 varints (7 bits per byte, high bit = "more bytes follow"), so the small
// numbers that dominate an index (position gaps, counts) take one byte instead of four or eight.

#include <cstdint>
#include <string>
#include <string_view>

#include "seek/index.hpp"

namespace seek::detail {

class Writer {
public:
    void raw(std::string_view bytes) { out_ += bytes; }

    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) out_ += static_cast<char>((value >> shift) & 0xFF);
    }

    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) out_ += static_cast<char>((value >> shift) & 0xFF);
    }

    void i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }

    void varint(std::uint64_t value) {
        while (value >= 0x80) {
            out_ += static_cast<char>((value & 0x7F) | 0x80);
            value >>= 7;
        }
        out_ += static_cast<char>(value);
    }

    void string(std::string_view s) {
        varint(s.size());
        out_ += s;
    }

    const std::string& bytes() const { return out_; }

private:
    std::string out_;
};

// Reads what Writer wrote. Every read is bounds-checked: a damaged file throws IndexFormatError
// instead of reading past the end of the buffer.
class Reader {
public:
    explicit Reader(std::string_view data) : data_(data) {}

    void expect(std::string_view magic) {
        if (data_.size() - pos_ < magic.size() || data_.substr(pos_, magic.size()) != magic) {
            throw IndexFormatError("not a seek index file");
        }
        pos_ += magic.size();
    }

    std::uint32_t u32() {
        need(4);
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) value |= std::uint32_t{next_byte()} << shift;
        return value;
    }

    std::uint64_t u64() {
        need(8);
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) value |= std::uint64_t{next_byte()} << shift;
        return value;
    }

    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }

    std::uint64_t varint() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            need(1);
            const unsigned char byte = next_byte();
            value |= std::uint64_t{byte & 0x7Fu} << shift;
            if ((byte & 0x80) == 0) return value;
        }
        throw IndexFormatError("malformed number");
    }

    std::string_view string() {
        const std::uint64_t size = varint();
        if (size > data_.size() - pos_) throw IndexFormatError("index file is truncated");
        const std::string_view s = data_.substr(pos_, static_cast<std::size_t>(size));
        pos_ += s.size();
        return s;
    }

    bool done() const { return pos_ == data_.size(); }

private:
    void need(std::size_t n) const {
        if (data_.size() - pos_ < n) throw IndexFormatError("index file is truncated");
    }

    unsigned char next_byte() { return static_cast<unsigned char>(data_[pos_++]); }

    std::string_view data_;
    std::size_t pos_ = 0;
};

}  // namespace seek::detail
