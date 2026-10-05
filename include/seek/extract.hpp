#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace seek {

// Thrown when a file can't be turned into text (unreadable, encrypted, too large, ...).
class ExtractError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The searchable text of one file.
struct ExtractedText {
    std::string text;                      // UTF-8; PDF pages are separated by '\f'
    std::vector<std::size_t> page_starts;  // byte offset where each PDF page begins; empty otherwise

    // 1-based page number containing the byte at `offset`, or 0 for files without pages.
    int page_at(std::size_t offset) const;
};

// True for file types seek knows how to read: .txt, .md, .markdown, .pdf (case-insensitive).
bool is_supported(const std::filesystem::path& path);

ExtractedText extract_text(const std::filesystem::path& path);

}  // namespace seek
