#include "seek/tokenizer.hpp"

namespace seek {
namespace {

constexpr std::size_t kMaxWordBytes = 64;

struct CodePoint {
    char32_t value = 0;
    std::size_t length = 1;  // bytes consumed
    bool valid = false;
};

CodePoint decode_utf8(std::string_view s, std::size_t i) {
    const auto lead = static_cast<unsigned char>(s[i]);
    if (lead < 0x80) return {lead, 1, true};

    const std::size_t length = lead >= 0xF8 ? 0 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 0;
    if (length == 0 || i + length > s.size()) return {};

    char32_t value = lead & (0x7Fu >> length);
    for (std::size_t k = 1; k < length; ++k) {
        const auto next = static_cast<unsigned char>(s[i + k]);
        if ((next & 0xC0) != 0x80) return {};
        value = (value << 6) | (next & 0x3Fu);
    }
    return {value, length, true};
}

bool is_word_char(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (c < 0xC0) return false;                    // Latin-1 punctuation: nbsp « » ° ¶ ...
    if (c == 0xD7 || c == 0xF7) return false;      // × ÷
    if (c >= 0x2000 && c <= 0x2BFF) return false;  // dashes, curly quotes, bullets, arrows, math
    if (c >= 0x3000 && c <= 0x303F) return false;  // CJK punctuation
    if (c == 0xFEFF || c == 0xFFFD) return false;  // byte-order mark, replacement character
    if (c >= 0x1F000) return false;                // emoji and pictographs
    return true;
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

void append_normalized(std::string& out, char32_t c, std::string_view original) {
    if (c >= 'A' && c <= 'Z') {
        out += static_cast<char>(c + ('a' - 'A'));
    } else if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) {
        append_utf8(out, c + 0x20);  // Latin-1 uppercase (À..Þ) sits exactly 0x20 below lowercase
    } else {
        switch (c) {
            case 0xFB00: out += "ff"; break;
            case 0xFB01: out += "fi"; break;
            case 0xFB02: out += "fl"; break;
            case 0xFB03: out += "ffi"; break;
            case 0xFB04: out += "ffl"; break;
            case 0xFB05:
            case 0xFB06: out += "st"; break;
            default: out += original;
        }
    }
}

}  // namespace

std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        CodePoint cp = decode_utf8(text, i);
        if (!cp.valid || !is_word_char(cp.value)) {
            i += cp.length;
            continue;
        }

        const std::size_t begin = i;
        std::string word;
        while (i < text.size()) {
            cp = decode_utf8(text, i);
            if (!cp.valid || !is_word_char(cp.value)) break;
            append_normalized(word, cp.value, text.substr(i, cp.length));
            i += cp.length;
        }
        if (i - begin <= kMaxWordBytes) tokens.push_back({std::move(word), begin, i});
    }
    return tokens;
}

}  // namespace seek
