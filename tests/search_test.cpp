#include "seek/search.hpp"

#include <gtest/gtest.h>

namespace seek {
namespace {

using Words = std::vector<std::string>;

std::vector<std::string> paths(const Index& index, const SearchResult& result) {
    std::vector<std::string> out;
    for (const Hit& hit : result.hits) out.push_back(index.documents()[hit.doc].path.string());
    return out;
}

TEST(ParseQuery, SeparateArgumentsAreOptionalWords) {
    const Query q = parse_query({"Kalman", "filter"});
    EXPECT_EQ(q.terms, (Words{"kalman", "filter"}));
    EXPECT_TRUE(q.phrases.empty());
}

TEST(ParseQuery, ShellQuotedArgumentIsAPhrase) {
    const Query q = parse_query({"kalman filter", "lidar"});
    EXPECT_EQ(q.terms, (Words{"lidar"}));
    EXPECT_EQ(q.phrases, (std::vector<Words>{{"kalman", "filter"}}));
}

TEST(ParseQuery, HyphenatedWordIsAPhrase) {
    EXPECT_EQ(parse_query({"state-space"}).phrases, (std::vector<Words>{{"state", "space"}}));
}

TEST(ParseQuery, LiteralQuotesMakeWordsRequired) {
    const Query q = parse_query({R"("lidar" fusion "point cloud")"});
    EXPECT_EQ(q.terms, (Words{"fusion"}));
    EXPECT_EQ(q.phrases, (std::vector<Words>{{"lidar"}, {"point", "cloud"}}));
}

TEST(ParseQuery, DropsDuplicatesAndPunctuation) {
    const Query q = parse_query({"radar", "RADAR", "!!!"});
    EXPECT_EQ(q.terms, (Words{"radar"}));
    EXPECT_TRUE(parse_query({"...", "--"}).empty());
}

TEST(ParseQuery, WordsCombinesTermsAndPhrasesWithoutDuplicates) {
    EXPECT_EQ(parse_query({"filter", "kalman filter"}).words(), (Words{"filter", "kalman"}));
}

class SearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        index.add({"/many.md", 0, 0, 0}, "kalman kalman kalman notes about the filter");
        index.add({"/once.md", 0, 0, 0}, "kalman notes about the filter and more words here");
        index.add({"/swapped.md", 0, 0, 0}, "filter kalman");
        index.add({"/common.md", 0, 0, 0}, "the the the notes notes");
        index.add({"/unrelated.md", 0, 0, 0}, "grocery list eggs milk");
    }
    Index index;
};

TEST_F(SearchTest, MoreOccurrencesRankHigher) {
    const SearchResult result = search(index, parse_query({"kalman"}), 10);
    ASSERT_EQ(result.total, 3u);
    EXPECT_EQ(paths(index, result).front(), "/many.md");
}

TEST_F(SearchTest, RareWordsOutweighCommonOnes) {
    // "the" is in 3 of 5 files (even 3 times in /common.md); "eggs" is in just one.
    // One rare match must beat several common ones.
    const SearchResult result = search(index, parse_query({"the", "eggs"}), 10);
    EXPECT_EQ(paths(index, result).front(), "/unrelated.md");
}

TEST_F(SearchTest, PhrasesMustMatchInOrder) {
    const SearchResult result = search(index, parse_query({"kalman filter"}), 10);
    EXPECT_EQ(result.total, 0u);  // "/swapped.md" has the words, but in the wrong order

    const SearchResult swapped = search(index, parse_query({"filter kalman"}), 10);
    EXPECT_EQ(paths(index, swapped), (Words{"/swapped.md"}));
}

TEST_F(SearchTest, PlainWordsOnlyReRankPhraseMatches) {
    const SearchResult result = search(index, parse_query({"kalman notes", "eggs"}), 10);
    EXPECT_EQ(result.total, 2u);  // eggs alone doesn't let /unrelated.md in
    for (const std::string& path : paths(index, result)) EXPECT_NE(path, "/unrelated.md");
}

TEST_F(SearchTest, EveryPhraseIsRequired) {
    EXPECT_EQ(search(index, parse_query({"kalman notes", "grocery list"}), 10).total, 0u);
}

TEST_F(SearchTest, LimitCapsHitsButNotTotal) {
    const SearchResult result = search(index, parse_query({"notes"}), 2);
    EXPECT_EQ(result.hits.size(), 2u);
    EXPECT_EQ(result.total, 3u);
    EXPECT_GE(result.hits[0].score, result.hits[1].score);
}

TEST_F(SearchTest, UnknownWordsFindNothing) {
    EXPECT_EQ(search(index, parse_query({"zebra"}), 10).total, 0u);
    EXPECT_EQ(search(Index{}, parse_query({"kalman"}), 10).total, 0u);
}

}  // namespace
}  // namespace seek
