#include "seek/tokenizer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace seek {
namespace {

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    for (const Token& token : tokenize(text)) out.push_back(token.text);
    return out;
}

TEST(Tokenizer, LowercasesAndSplitsOnPunctuation) {
    EXPECT_EQ(words("The Kalman-filter, v2!"), (std::vector<std::string>{"the", "kalman", "filter", "v2"}));
}

TEST(Tokenizer, RecordsByteOffsetsIntoTheOriginalText) {
    const std::string text = "  Hello, World";
    const auto tokens = tokenize(text);
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(text.substr(tokens[0].begin, tokens[0].end - tokens[0].begin), "Hello");
    EXPECT_EQ(text.substr(tokens[1].begin, tokens[1].end - tokens[1].begin), "World");
}

TEST(Tokenizer, KeepsAccentedWordsWholeAndLowercasesLatin1) {
    EXPECT_EQ(words("Café ÉCOLE naïve"), (std::vector<std::string>{"café", "école", "naïve"}));
}

TEST(Tokenizer, TreatsTypographicPunctuationAsSeparators) {
    // Curly quotes, en/em dashes, and bullets are common in PDFs and must not glue onto words.
    EXPECT_EQ(words("\u201Clidar\u201D\u2013radar\u2014sonar \u2022 camera"),
              (std::vector<std::string>{"lidar", "radar", "sonar", "camera"}));
}

TEST(Tokenizer, ExpandsPdfLigatures) {
    EXPECT_EQ(words("\uFB01lter \uFB02ow e\uFB03cient"), (std::vector<std::string>{"filter", "flow", "efficient"}));
}

TEST(Tokenizer, SkipsOverlongWordsAndInvalidUtf8) {
    const std::string blob(100, 'a');
    EXPECT_EQ(words("start " + blob + " end"), (std::vector<std::string>{"start", "end"}));
    EXPECT_EQ(words("ok\xFF\xFEyes"), (std::vector<std::string>{"ok", "yes"}));
    EXPECT_EQ(words("cut\xE2\x80"), (std::vector<std::string>{"cut"}));  // truncated sequence at end
}

TEST(Tokenizer, EmptyAndSeparatorOnlyInput) {
    EXPECT_TRUE(tokenize("").empty());
    EXPECT_TRUE(tokenize(" \n\t--- ...").empty());
}

}  // namespace
}  // namespace seek
