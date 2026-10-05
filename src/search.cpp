#include "seek/search.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string_view>
#include <unordered_map>

#include "seek/tokenizer.hpp"

namespace seek {
namespace {

// Standard BM25 constants. k1 controls how fast repeated words stop adding score (saturation);
// b controls how much long documents are penalized.
constexpr double kK1 = 1.2;
constexpr double kB = 0.75;

void add_chunk(std::string_view chunk, bool quoted, Query& query) {
    std::vector<std::string> words;
    for (Token& token : tokenize(chunk)) words.push_back(std::move(token.text));
    if (words.empty()) return;
    if (words.size() == 1 && !quoted) {
        query.terms.push_back(std::move(words.front()));
    } else {
        query.phrases.push_back(std::move(words));
    }
}

void add_unquoted(std::string_view text, Query& query) {
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const std::size_t begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > begin) add_chunk(text.substr(begin, i - begin), false, query);
    }
}

template <typename T>
void remove_duplicates(std::vector<T>& items) {
    std::set<T> seen;
    std::erase_if(items, [&](const T& item) { return !seen.insert(item).second; });
}

const Posting* find_posting(const std::vector<Posting>& list, DocId doc) {
    const auto it = std::ranges::lower_bound(list, doc, {}, &Posting::doc);
    return it != list.end() && it->doc == doc ? &*it : nullptr;
}

// For each document containing the phrase, how many times it occurs. The phrase occurs at
// position p when word 0 is at p, word 1 at p+1, and so on.
std::vector<std::pair<DocId, std::uint32_t>> phrase_matches(const Index& index, const std::vector<std::string>& words) {
    std::vector<const std::vector<Posting>*> lists;
    for (const std::string& word : words) {
        lists.push_back(&index.postings(word));
        if (lists.back()->empty()) return {};
    }

    std::vector<std::pair<DocId, std::uint32_t>> matches;
    std::vector<const Posting*> rest(words.size() - 1);
    for (const Posting& first : *lists.front()) {
        bool in_all = true;
        for (std::size_t i = 1; i < lists.size() && in_all; ++i) {
            rest[i - 1] = find_posting(*lists[i], first.doc);
            in_all = rest[i - 1] != nullptr;
        }
        if (!in_all) continue;

        std::uint32_t count = 0;
        for (const std::uint32_t start : first.positions) {
            bool whole = true;
            for (std::size_t i = 0; i < rest.size() && whole; ++i) {
                whole = std::ranges::binary_search(rest[i]->positions, start + static_cast<std::uint32_t>(i) + 1);
            }
            if (whole) ++count;
        }
        if (count > 0) matches.emplace_back(first.doc, count);
    }
    return matches;
}

}  // namespace

std::vector<std::string> Query::words() const {
    std::vector<std::string> all = terms;
    for (const auto& phrase : phrases) all.insert(all.end(), phrase.begin(), phrase.end());
    remove_duplicates(all);
    return all;
}

Query parse_query(const std::vector<std::string>& args) {
    Query query;
    for (const std::string& arg : args) {
        if (arg.find('"') == std::string::npos) {
            add_chunk(arg, false, query);
            continue;
        }
        // Literal quotes: text alternates between outside and inside a quoted phrase.
        bool inside = false;
        std::size_t begin = 0;
        for (std::size_t i = 0; i <= arg.size(); ++i) {
            if (i < arg.size() && arg[i] != '"') continue;
            const std::string_view segment(arg.data() + begin, i - begin);
            if (inside) {
                add_chunk(segment, true, query);
            } else {
                add_unquoted(segment, query);
            }
            inside = !inside;
            begin = i + 1;
        }
    }
    remove_duplicates(query.terms);
    remove_duplicates(query.phrases);
    return query;
}

SearchResult search(const Index& index, const Query& query, std::size_t limit) {
    const std::vector<Document>& docs = index.documents();
    if (docs.empty() || query.empty()) return {};

    const double doc_count = static_cast<double>(docs.size());
    const double average_length = std::max(index.average_length(), 1.0);
    const auto bm25 = [&](double frequency, std::size_t docs_with_word, DocId doc) {
        // Rare words are worth more: "kalman" in 3 of 400 files beats "the" in 399 of them.
        const double df = static_cast<double>(docs_with_word);
        const double idf = std::log(1.0 + (doc_count - df + 0.5) / (df + 0.5));
        const double length_ratio = static_cast<double>(docs[doc].length) / average_length;
        return idf * frequency * (kK1 + 1) / (frequency + kK1 * (1 - kB + kB * length_ratio));
    };

    // Phrases are required: keep only documents that contain every one of them.
    std::unordered_map<DocId, double> scores;
    for (std::size_t i = 0; i < query.phrases.size(); ++i) {
        const auto matches = phrase_matches(index, query.phrases[i]);
        std::unordered_map<DocId, double> kept;
        for (const auto& [doc, count] : matches) {
            const auto previous = scores.find(doc);
            if (i > 0 && previous == scores.end()) continue;
            const double so_far = i > 0 ? previous->second : 0.0;
            kept[doc] = so_far + bm25(count, matches.size(), doc);
        }
        scores = std::move(kept);
        if (scores.empty()) return {};
    }

    // Plain words add to the score. With phrases present, they only re-rank phrase matches.
    const bool restricted = !query.phrases.empty();
    for (const std::string& term : query.terms) {
        const std::vector<Posting>& list = index.postings(term);
        for (const Posting& posting : list) {
            const double score = bm25(static_cast<double>(posting.positions.size()), list.size(), posting.doc);
            if (!restricted) {
                scores[posting.doc] += score;
            } else if (const auto it = scores.find(posting.doc); it != scores.end()) {
                it->second += score;
            }
        }
    }

    SearchResult result;
    result.total = scores.size();
    result.hits.reserve(scores.size());
    for (const auto& [doc, score] : scores) result.hits.push_back({doc, score});

    // Only the top `limit` need sorting; ties break by id so output is deterministic.
    const auto better = [](const Hit& a, const Hit& b) {
        return a.score != b.score ? a.score > b.score : a.doc < b.doc;
    };
    const std::size_t shown = std::min(limit, result.hits.size());
    std::partial_sort(result.hits.begin(), result.hits.begin() + static_cast<std::ptrdiff_t>(shown), result.hits.end(),
                      better);
    result.hits.resize(shown);
    return result;
}

}  // namespace seek
