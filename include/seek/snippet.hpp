#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "seek/extract.hpp"

namespace seek {

// A short excerpt of a document around the best cluster of query words.
struct Snippet {
    std::string text;
    std::vector<std::pair<std::size_t, std::size_t>> highlights;  // [begin, end) byte ranges in `text`
    int page = 0;                                                 // 1-based PDF page, or 0
};

// Picks the window of `window` words containing the most distinct query words (then the most
// matches overall), collapses whitespace and control characters, and marks the matches.
Snippet make_snippet(const ExtractedText& source, const std::vector<std::string>& words, std::size_t window = 28);

}  // namespace seek
