#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace seek {

using DocId = std::uint32_t;

// One indexed file. `mtime` and `size` are the file's stamp when it was read; if either changes,
// the next sync re-reads the file.
struct Document {
    std::filesystem::path path;
    std::int64_t mtime = 0;
    std::uint64_t size = 0;
    std::uint32_t length = 0;  // word count; BM25 uses it so long files don't win just by being long
};

// Where one word appears inside one document.
struct Posting {
    DocId doc = 0;
    std::vector<std::uint32_t> positions;  // word positions in the document, ascending
};

// Thrown when an index file is corrupted, truncated, or written by another format version.
class IndexFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// An inverted index: for every word, the documents (and positions) it appears in. It is the
// index at the back of a textbook, built for every word of every file.
//
// Invariant: every posting list is sorted by DocId. New documents always get the next id, and
// removal keeps relative order, so lists can be binary-searched by document.
class Index {
public:
    // Reads an index from disk. A missing file yields an empty index.
    static Index load(const std::filesystem::path& file);

    // Writes atomically (temp file + rename), so a crash mid-save never corrupts the old index.
    void save(const std::filesystem::path& file) const;

    // Folders the user asked to index (absolute paths).
    const std::vector<std::filesystem::path>& roots() const { return roots_; }
    bool add_root(const std::filesystem::path& root);
    bool remove_root(const std::filesystem::path& root);

    DocId add(Document doc, std::string_view text);

    // Removes matching documents and renumbers the rest. Returns how many were removed.
    std::size_t remove_if(const std::function<bool(const Document&)>& should_remove);

    const std::vector<Document>& documents() const { return docs_; }
    const std::vector<Posting>& postings(std::string_view term) const;  // empty if absent
    std::size_t term_count() const { return postings_.size(); }
    double average_length() const;

    std::int64_t updated_at() const { return updated_at_; }  // unix seconds, 0 if never synced
    void mark_updated();

private:
    // Lets postings_.find() take a string_view without allocating a std::string per lookup.
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    };

    std::vector<std::filesystem::path> roots_;
    std::vector<Document> docs_;
    std::unordered_map<std::string, std::vector<Posting>, StringHash, std::equal_to<>> postings_;
    std::uint64_t total_length_ = 0;
    std::int64_t updated_at_ = 0;
};

}  // namespace seek
