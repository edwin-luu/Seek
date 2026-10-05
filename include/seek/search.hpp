#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "seek/index.hpp"

namespace seek {

struct Query {
    std::vector<std::string> terms;                 // optional words: more matches rank higher
    std::vector<std::vector<std::string>> phrases;  // required: words must appear together, in order

    bool empty() const { return terms.empty() && phrases.empty(); }

    // Every distinct word in the query (used to highlight snippets).
    std::vector<std::string> words() const;
};

// Builds a query from command-line arguments, where the shell has already removed the quotes:
//   seek kalman filter        -> two optional words
//   seek "kalman filter"      -> one argument with a space -> a required phrase
//   seek kalman-filter        -> one argument, two words -> a required phrase
//   seek '"lidar" filter'     -> literal quotes still work: "lidar" becomes required
Query parse_query(const std::vector<std::string>& args);

struct Hit {
    DocId doc = 0;
    double score = 0;
};

struct SearchResult {
    std::vector<Hit> hits;  // best first, at most `limit`
    std::size_t total = 0;  // how many documents matched in all
};

// Ranks documents with BM25: a word counts more the more often it appears in a document (with
// diminishing returns), the rarer it is across all documents, and the shorter the document.
// A document must contain every phrase; plain words only add to its score.
SearchResult search(const Index& index, const Query& query, std::size_t limit);

}  // namespace seek
