#include "seek/index.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>

#include "binary_io.hpp"
#include "seek/tokenizer.hpp"

namespace seek {
namespace fs = std::filesystem;

namespace {

// File layout (all counts and gaps are varints, see binary_io.hpp):
//
//   magic "SEEKIDX\0" | u32 version | i64 updated_at
//   roots:     count, then each path
//   documents: count, then each { path, i64 mtime, size, length }
//   terms:     count, then each { term, posting count, then each posting:
//                { doc id gap, position count, position gaps } }
//
// Doc ids and positions are stored as gaps from the previous value ("delta encoding"): sorted
// numbers like 1040, 1043, 1051 become 1040, 3, 8, which fit in a single varint byte each.
constexpr std::string_view kMagic{"SEEKIDX\0", 8};
constexpr std::uint32_t kFormatVersion = 1;

std::uint32_t to_u32(std::uint64_t value, const char* what) {
    if (value > std::numeric_limits<std::uint32_t>::max()) throw IndexFormatError(std::string(what) + " out of range");
    return static_cast<std::uint32_t>(value);
}

}  // namespace

Index Index::load(const fs::path& file) {
    Index index;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::error_code ec;
        if (!fs::exists(file, ec)) return index;
        throw IndexFormatError("cannot open " + file.string());
    }
    const std::string data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

    detail::Reader r(data);
    r.expect(kMagic);
    if (const std::uint32_t version = r.u32(); version != kFormatVersion) {
        throw IndexFormatError("index format version " + std::to_string(version) + " is not supported");
    }
    index.updated_at_ = r.i64();

    for (std::uint64_t n = r.varint(); n > 0; --n) index.roots_.emplace_back(r.string());

    for (std::uint64_t n = r.varint(); n > 0; --n) {
        Document doc;
        doc.path = r.string();
        doc.mtime = r.i64();
        doc.size = r.varint();
        doc.length = to_u32(r.varint(), "document length");
        index.total_length_ += doc.length;
        index.docs_.push_back(std::move(doc));
    }

    for (std::uint64_t n = r.varint(); n > 0; --n) {
        auto [it, inserted] = index.postings_.try_emplace(std::string(r.string()));
        if (!inserted) throw IndexFormatError("duplicate term");
        std::vector<Posting>& list = it->second;

        std::uint64_t doc = 0;
        for (std::uint64_t p = r.varint(); p > 0; --p) {
            const std::uint64_t gap = r.varint();
            if (!list.empty() && gap == 0) throw IndexFormatError("posting list out of order");
            doc += gap;
            if (doc >= index.docs_.size()) throw IndexFormatError("posting refers to a missing document");

            Posting posting{static_cast<DocId>(doc), {}};
            const std::uint32_t length = index.docs_[posting.doc].length;
            std::uint64_t pos = 0;
            for (std::uint64_t k = r.varint(); k > 0; --k) {
                const std::uint64_t pos_gap = r.varint();
                if (!posting.positions.empty() && pos_gap == 0) throw IndexFormatError("positions out of order");
                pos += pos_gap;
                if (pos >= length) throw IndexFormatError("position past end of document");
                posting.positions.push_back(static_cast<std::uint32_t>(pos));
            }
            if (posting.positions.empty()) throw IndexFormatError("empty posting");
            list.push_back(std::move(posting));
        }
        if (list.empty()) throw IndexFormatError("term without postings");
    }

    if (!r.done()) throw IndexFormatError("unexpected data at end of index file");
    return index;
}

void Index::save(const fs::path& file) const {
    detail::Writer w;
    w.raw(kMagic);
    w.u32(kFormatVersion);
    w.i64(updated_at_);

    w.varint(roots_.size());
    for (const fs::path& root : roots_) w.string(root.string());

    w.varint(docs_.size());
    for (const Document& doc : docs_) {
        w.string(doc.path.string());
        w.i64(doc.mtime);
        w.varint(doc.size);
        w.varint(doc.length);
    }

    // Sorted so the same index always produces byte-identical files.
    std::vector<const decltype(postings_)::value_type*> terms;
    terms.reserve(postings_.size());
    for (const auto& entry : postings_) terms.push_back(&entry);
    std::ranges::sort(terms, {}, [](const auto* entry) { return std::string_view(entry->first); });

    w.varint(terms.size());
    for (const auto* entry : terms) {
        w.string(entry->first);
        w.varint(entry->second.size());
        DocId previous_doc = 0;
        for (const Posting& posting : entry->second) {
            w.varint(posting.doc - previous_doc);
            previous_doc = posting.doc;
            w.varint(posting.positions.size());
            std::uint32_t previous_pos = 0;
            for (const std::uint32_t pos : posting.positions) {
                w.varint(pos - previous_pos);
                previous_pos = pos;
            }
        }
    }

    if (file.has_parent_path()) fs::create_directories(file.parent_path());
    fs::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(w.bytes().data(), static_cast<std::streamsize>(w.bytes().size()));
        out.flush();
        if (!out) throw std::runtime_error("cannot write " + temp.string());
    }
    fs::rename(temp, file);  // atomic on POSIX: readers see the old file or the new one, never half
}

bool Index::add_root(const fs::path& root) {
    if (std::ranges::find(roots_, root) != roots_.end()) return false;
    roots_.push_back(root);
    return true;
}

bool Index::remove_root(const fs::path& root) {
    return std::erase(roots_, root) > 0;
}

DocId Index::add(Document doc, std::string_view text) {
    if (docs_.size() >= std::numeric_limits<DocId>::max()) throw std::length_error("too many documents");
    const auto id = static_cast<DocId>(docs_.size());

    const std::vector<Token> tokens = tokenize(text);
    if (tokens.size() > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("document too long");
    doc.length = static_cast<std::uint32_t>(tokens.size());

    // Group positions by word first, so each word's posting list grows by exactly one entry.
    std::unordered_map<std::string_view, std::vector<std::uint32_t>> positions;
    for (std::uint32_t pos = 0; pos < doc.length; ++pos) positions[tokens[pos].text].push_back(pos);

    for (auto& [term, list] : positions) {
        auto it = postings_.find(term);
        if (it == postings_.end()) it = postings_.emplace(std::string(term), std::vector<Posting>{}).first;
        it->second.push_back(Posting{id, std::move(list)});  // id is the largest so far: stays sorted
    }

    total_length_ += doc.length;
    docs_.push_back(std::move(doc));
    return id;
}

std::size_t Index::remove_if(const std::function<bool(const Document&)>& should_remove) {
    constexpr DocId kRemoved = std::numeric_limits<DocId>::max();

    // Decide which documents survive and give them new, gap-free ids (order preserved).
    std::vector<DocId> new_id(docs_.size());
    std::vector<Document> kept;
    for (std::size_t old = 0; old < docs_.size(); ++old) {
        if (should_remove(docs_[old])) {
            new_id[old] = kRemoved;
        } else {
            new_id[old] = static_cast<DocId>(kept.size());
            kept.push_back(std::move(docs_[old]));
        }
    }
    const std::size_t removed = docs_.size() - kept.size();
    docs_ = std::move(kept);
    if (removed == 0) return 0;

    // One pass over every posting list: drop removed docs, renumber the rest, drop empty words.
    for (auto it = postings_.begin(); it != postings_.end();) {
        std::vector<Posting>& list = it->second;
        std::erase_if(list, [&](const Posting& p) { return new_id[p.doc] == kRemoved; });
        for (Posting& p : list) p.doc = new_id[p.doc];
        it = list.empty() ? postings_.erase(it) : std::next(it);
    }

    total_length_ = 0;
    for (const Document& doc : docs_) total_length_ += doc.length;
    return removed;
}

const std::vector<Posting>& Index::postings(std::string_view term) const {
    static const std::vector<Posting> kNone;
    const auto it = postings_.find(term);
    return it == postings_.end() ? kNone : it->second;
}

double Index::average_length() const {
    return docs_.empty() ? 0.0 : static_cast<double>(total_length_) / static_cast<double>(docs_.size());
}

void Index::mark_updated() {
    using namespace std::chrono;
    updated_at_ = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace seek
