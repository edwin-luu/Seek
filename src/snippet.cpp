#include "seek/snippet.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_set>

#include "seek/tokenizer.hpp"

namespace seek {
namespace {

constexpr std::size_t kWordsBeforeMatch = 6;  // a little lead-in context before the first match
constexpr std::size_t kMaxGapBytes = 24;      // longer runs of punctuation (tables, rules) get elided

// Text between two words: whitespace and control characters (including ANSI escape bytes from
// hostile files) collapse to one space, so a snippet always prints on a single clean line.
void append_gap(std::string& out, std::string_view gap) {
    std::string cleaned;
    bool space = false;
    for (const char c : gap) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte == 0x7F) {
            space = true;
            continue;
        }
        if (space) cleaned += ' ';
        space = false;
        cleaned += c;
    }
    if (space) cleaned += ' ';
    out += cleaned.size() > kMaxGapBytes ? " … " : cleaned;
}

}  // namespace

Snippet make_snippet(const ExtractedText& source, const std::vector<std::string>& words, std::size_t window) {
    Snippet snippet;
    const std::vector<Token> tokens = tokenize(source.text);
    if (tokens.empty() || window == 0) return snippet;

    const std::unordered_set<std::string_view> wanted(words.begin(), words.end());
    std::vector<std::size_t> hits;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (wanted.contains(tokens[i].text)) hits.push_back(i);
    }

    // Try a window starting just before each match and keep the best one.
    std::size_t best_start = 0;
    std::size_t best_hit = 0;
    std::pair<std::size_t, std::size_t> best_score{0, 0};  // (distinct words, total matches)
    for (std::size_t h = 0; h < hits.size(); ++h) {
        const std::size_t start = hits[h] >= kWordsBeforeMatch ? hits[h] - kWordsBeforeMatch : 0;
        std::unordered_set<std::string_view> distinct;
        std::size_t total = 0;
        for (std::size_t k = h; k < hits.size() && hits[k] < start + window; ++k) {
            distinct.insert(tokens[hits[k]].text);
            ++total;
        }
        const std::pair<std::size_t, std::size_t> score{distinct.size(), total};
        if (score > best_score) {
            best_score = score;
            best_start = start;
            best_hit = hits[h];
        }
    }

    const std::size_t end = std::min(best_start + window, tokens.size());
    if (best_start > 0) snippet.text += "… ";
    for (std::size_t i = best_start; i < end; ++i) {
        if (i > best_start) {
            const std::size_t gap_begin = tokens[i - 1].end;
            append_gap(snippet.text, std::string_view(source.text).substr(gap_begin, tokens[i].begin - gap_begin));
        }
        const std::size_t begin = snippet.text.size();
        snippet.text.append(source.text, tokens[i].begin, tokens[i].end - tokens[i].begin);
        if (wanted.contains(tokens[i].text)) snippet.highlights.emplace_back(begin, snippet.text.size());
    }
    if (end < tokens.size()) snippet.text += " …";

    snippet.page = source.page_at(tokens[best_hit].begin);
    return snippet;
}

}  // namespace seek
