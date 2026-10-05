#include "seek/index.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "test_support.hpp"

namespace seek {
namespace {

Index sample_index() {
    Index index;
    index.add_root("/notes");
    index.add({"/notes/a.md", 100, 10, 0}, "kalman filter for kalman people");
    index.add({"/notes/b.md", 200, 20, 0}, "particle filter");
    index.add({"/notes/c.md", 300, 30, 0}, "lidar point clouds");
    return index;
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

TEST(Index, RecordsDocumentsPositionsAndLengths) {
    const Index index = sample_index();
    ASSERT_EQ(index.documents().size(), 3u);
    EXPECT_EQ(index.documents()[0].length, 5u);

    const auto& kalman = index.postings("kalman");
    ASSERT_EQ(kalman.size(), 1u);
    EXPECT_EQ(kalman[0].doc, 0u);
    EXPECT_EQ(kalman[0].positions, (std::vector<std::uint32_t>{0, 3}));

    const auto& filter = index.postings("filter");
    ASSERT_EQ(filter.size(), 2u);
    EXPECT_EQ(filter[0].doc, 0u);
    EXPECT_EQ(filter[1].doc, 1u);

    EXPECT_TRUE(index.postings("missing").empty());
    EXPECT_DOUBLE_EQ(index.average_length(), (5.0 + 2.0 + 3.0) / 3.0);
}

TEST(Index, RemoveRenumbersDocumentsAndDropsUnusedWords) {
    Index index = sample_index();
    EXPECT_EQ(index.remove_if([](const Document& d) { return d.path == "/notes/a.md"; }), 1u);

    ASSERT_EQ(index.documents().size(), 2u);
    EXPECT_EQ(index.documents()[0].path, "/notes/b.md");
    EXPECT_TRUE(index.postings("kalman").empty());

    const auto& filter = index.postings("filter");
    ASSERT_EQ(filter.size(), 1u);
    EXPECT_EQ(filter[0].doc, 0u);  // b.md moved from id 1 to id 0
    EXPECT_EQ(index.postings("lidar")[0].doc, 1u);
    EXPECT_DOUBLE_EQ(index.average_length(), 2.5);
}

TEST(Index, AddAfterRemoveKeepsPostingListsSorted) {
    Index index = sample_index();
    index.remove_if([](const Document& d) { return d.path == "/notes/b.md"; });
    index.add({"/notes/d.md", 1, 1, 0}, "filter");
    const auto& filter = index.postings("filter");
    ASSERT_EQ(filter.size(), 2u);
    EXPECT_LT(filter[0].doc, filter[1].doc);
}

TEST(Index, SaveThenLoadRoundTrips) {
    const test::TempDir dir;
    Index original = sample_index();
    original.mark_updated();
    original.save(dir / "index.bin");

    const Index loaded = Index::load(dir / "index.bin");
    EXPECT_EQ(loaded.roots(), original.roots());
    EXPECT_EQ(loaded.updated_at(), original.updated_at());
    ASSERT_EQ(loaded.documents().size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(loaded.documents()[i].path, original.documents()[i].path);
        EXPECT_EQ(loaded.documents()[i].mtime, original.documents()[i].mtime);
        EXPECT_EQ(loaded.documents()[i].size, original.documents()[i].size);
        EXPECT_EQ(loaded.documents()[i].length, original.documents()[i].length);
    }
    EXPECT_EQ(loaded.term_count(), original.term_count());
    EXPECT_EQ(loaded.postings("kalman")[0].positions, (std::vector<std::uint32_t>{0, 3}));
    EXPECT_DOUBLE_EQ(loaded.average_length(), original.average_length());

    // Saving is deterministic: same index, same bytes.
    loaded.save(dir / "again.bin");
    EXPECT_EQ(read_bytes(dir / "index.bin"), read_bytes(dir / "again.bin"));
}

TEST(Index, LoadingAMissingFileGivesAnEmptyIndex) {
    const test::TempDir dir;
    const Index index = Index::load(dir / "nope.bin");
    EXPECT_TRUE(index.documents().empty());
    EXPECT_TRUE(index.roots().empty());
}

TEST(Index, RejectsFilesThatAreNotIndexes) {
    const test::TempDir dir;
    test::write_file(dir / "junk.bin", "definitely not an index");
    EXPECT_THROW(Index::load(dir / "junk.bin"), IndexFormatError);
}

TEST(Index, RejectsEveryTruncationOfAValidFile) {
    const test::TempDir dir;
    sample_index().save(dir / "index.bin");
    const std::string bytes = read_bytes(dir / "index.bin");
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        test::write_file(dir / "cut.bin", std::string_view(bytes).substr(0, cut));
        EXPECT_THROW(Index::load(dir / "cut.bin"), IndexFormatError) << "cut at byte " << cut;
    }
}

TEST(Index, RejectsTrailingGarbage) {
    const test::TempDir dir;
    sample_index().save(dir / "index.bin");
    test::write_file(dir / "long.bin", read_bytes(dir / "index.bin") + "x");
    EXPECT_THROW(Index::load(dir / "long.bin"), IndexFormatError);
}

TEST(Index, RejectsOtherFormatVersions) {
    const test::TempDir dir;
    sample_index().save(dir / "index.bin");
    std::string bytes = read_bytes(dir / "index.bin");
    bytes[8] = 99;  // the version number follows the 8-byte magic
    test::write_file(dir / "future.bin", bytes);
    EXPECT_THROW(Index::load(dir / "future.bin"), IndexFormatError);
}

TEST(Index, RootsAreUnique) {
    Index index;
    EXPECT_TRUE(index.add_root("/a"));
    EXPECT_FALSE(index.add_root("/a"));
    EXPECT_TRUE(index.remove_root("/a"));
    EXPECT_FALSE(index.remove_root("/a"));
}

}  // namespace
}  // namespace seek
