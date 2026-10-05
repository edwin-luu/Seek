#include "seek/snippet.hpp"

#include <gtest/gtest.h>

namespace seek {
namespace {

std::vector<std::string> highlighted(const Snippet& snippet) {
    std::vector<std::string> out;
    for (const auto& [begin, end] : snippet.highlights) out.push_back(snippet.text.substr(begin, end - begin));
    return out;
}

TEST(Snippet, HighlightsMatchesInTheirOriginalCase) {
    const Snippet s = make_snippet({"The Kalman Filter is great.", {}}, {"kalman", "filter"});
    EXPECT_EQ(s.text, "The Kalman Filter is great");
    EXPECT_EQ(highlighted(s), (std::vector<std::string>{"Kalman", "Filter"}));
    EXPECT_EQ(s.page, 0);
}

TEST(Snippet, PrefersTheWindowWithTheMostDistinctWords) {
    std::string text = "lidar alone here. ";
    for (int i = 0; i < 50; ++i) text += "filler ";
    text += "lidar and radar together.";
    const Snippet s = make_snippet({text, {}}, {"lidar", "radar"}, 10);
    EXPECT_EQ(highlighted(s), (std::vector<std::string>{"lidar", "radar"}));
    EXPECT_TRUE(s.text.starts_with("… "));
    EXPECT_FALSE(s.text.ends_with("…"));
}

TEST(Snippet, CollapsesWhitespaceAndStripsControlCharacters) {
    const Snippet s = make_snippet({"one\n\n\ttwo\x1b[31m three", {}}, {"two"});
    EXPECT_EQ(s.text, "one two [31m three");  // ESC removed, so no terminal escape survives
}

TEST(Snippet, ElidesLongRunsOfPunctuation) {
    const Snippet s = make_snippet({"alpha ------------------------------------ beta", {}}, {"beta"});
    EXPECT_EQ(s.text, "alpha … beta");
}

TEST(Snippet, ReportsThePageOfTheMatch) {
    ExtractedText pdf;
    pdf.page_starts = {0};
    pdf.text = "first page words\f";
    pdf.page_starts.push_back(pdf.text.size());
    pdf.text += "second page has the answer\f";
    EXPECT_EQ(make_snippet(pdf, {"answer"}).page, 2);
}

TEST(Snippet, EmptyTextGivesEmptySnippet) {
    EXPECT_TRUE(make_snippet({"", {}}, {"x"}).text.empty());
}

}  // namespace
}  // namespace seek
