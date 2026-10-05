#include "seek/extract.hpp"

#include <gtest/gtest.h>

#include "test_support.hpp"

namespace seek {
namespace {

TEST(Extract, RecognizesSupportedExtensionsCaseInsensitively) {
    EXPECT_TRUE(is_supported("notes.md"));
    EXPECT_TRUE(is_supported("README.MARKDOWN"));
    EXPECT_TRUE(is_supported("a.TXT"));
    EXPECT_TRUE(is_supported("paper.Pdf"));
    EXPECT_FALSE(is_supported("photo.jpg"));
    EXPECT_FALSE(is_supported("Makefile"));
}

TEST(Extract, ReadsTextFilesVerbatim) {
    const test::TempDir dir;
    test::write_file(dir / "a.md", "# Title\nbody text");
    const ExtractedText text = extract_text(dir / "a.md");
    EXPECT_EQ(text.text, "# Title\nbody text");
    EXPECT_TRUE(text.page_starts.empty());
    EXPECT_EQ(text.page_at(3), 0);
}

TEST(Extract, ReadsPdfTextPageByPage) {
    const test::TempDir dir;
    test::write_pdf(dir / "doc.pdf", {"Kalman filter basics", "Particle filter details"});
    const ExtractedText pdf = extract_text(dir / "doc.pdf");

    ASSERT_EQ(pdf.page_starts.size(), 2u);
    const auto second = pdf.text.find("Particle");
    ASSERT_NE(pdf.text.find("Kalman filter basics"), std::string::npos);
    ASSERT_NE(second, std::string::npos);
    EXPECT_EQ(pdf.page_at(pdf.text.find("Kalman")), 1);
    EXPECT_EQ(pdf.page_at(second), 2);
}

TEST(Extract, ReportsBrokenFiles) {
    const test::TempDir dir;
    test::write_file(dir / "fake.pdf", "this is not a pdf");
    EXPECT_THROW(extract_text(dir / "fake.pdf"), ExtractError);
    EXPECT_THROW(extract_text(dir / "missing.txt"), ExtractError);
}

}  // namespace
}  // namespace seek
