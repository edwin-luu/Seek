#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace seek {

// One word found in a piece of text.
struct Token {
    std::string text;   // normalized form used for matching ("Kalman" -> "kalman")
    std::size_t begin;  // byte offset of the word in the original text
    std::size_t end;    // one past the word's last byte
};

// Splits UTF-8 text into words. A word is a run of letters and digits, including non-English
// letters ("café" stays one word). Punctuation and symbols separate words. Normalization folds
// ASCII and Latin-1 letters to lowercase and expands PDF ligatures ("ﬁ" -> "fi"). Words longer
// than 64 bytes (base64 blobs, hashes) are skipped. Invalid UTF-8 bytes are treated as separators.
std::vector<Token> tokenize(std::string_view text);

}  // namespace seek
