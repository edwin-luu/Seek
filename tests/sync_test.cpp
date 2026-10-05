#include "seek/sync.hpp"

#include <gtest/gtest.h>

#include "seek/search.hpp"
#include "test_support.hpp"

namespace seek {
namespace {

std::size_t hits_for(const Index& index, const std::string& word) {
    return search(index, parse_query({word}), 100).total;
}

class SyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::write_file(dir / "notes/robotics.md", "kalman filter notes");
        test::write_file(dir / "notes/todo.txt", "buy eggs");
        test::write_pdf(dir / "notes/paper.pdf", {"intro", "lidar point clouds"});
        index.add_root(dir.path());
    }
    test::TempDir dir;
    Index index;
};

TEST_F(SyncTest, IndexesSupportedFilesOnFirstRun) {
    const SyncReport report = sync(index);
    EXPECT_EQ(report.added, 3u);
    EXPECT_EQ(index.documents().size(), 3u);
    EXPECT_EQ(hits_for(index, "lidar"), 1u);
    EXPECT_GT(index.updated_at(), 0);
}

TEST_F(SyncTest, SecondRunReadsNothingWhenNothingChanged) {
    sync(index);
    const SyncReport report = sync(index);
    EXPECT_EQ(report.unchanged, 3u);
    EXPECT_EQ(report.added + report.updated + report.removed, 0u);
}

TEST_F(SyncTest, PicksUpEditsAdditionsAndDeletions) {
    sync(index);
    test::write_file(dir / "notes/robotics.md", "particle filter notes");
    test::bump_mtime(dir / "notes/robotics.md");
    test::write_file(dir / "notes/new.md", "radar");
    std::filesystem::remove(dir / "notes/todo.txt");

    const SyncReport report = sync(index);
    EXPECT_EQ(report.added, 1u);
    EXPECT_EQ(report.updated, 1u);
    EXPECT_EQ(report.removed, 1u);
    EXPECT_EQ(report.unchanged, 1u);
    EXPECT_EQ(hits_for(index, "kalman"), 0u);
    EXPECT_EQ(hits_for(index, "particle"), 1u);
    EXPECT_EQ(hits_for(index, "radar"), 1u);
    EXPECT_EQ(hits_for(index, "eggs"), 0u);
}

TEST_F(SyncTest, IgnoresHiddenFoldersAndUnsupportedFiles) {
    test::write_file(dir / ".git/notes.md", "secret");
    test::write_file(dir / "node_modules/pkg/readme.md", "secret");
    test::write_file(dir / "photo.jpg", "secret");
    sync(index);
    EXPECT_EQ(index.documents().size(), 3u);
    EXPECT_EQ(hits_for(index, "secret"), 0u);
}

TEST_F(SyncTest, ReportsUnreadableFilesWithoutStopping) {
    test::write_file(dir / "broken.pdf", "not a pdf");
    const SyncReport report = sync(index);
    EXPECT_EQ(report.added, 3u);
    ASSERT_EQ(report.skipped.size(), 1u);
    EXPECT_EQ(report.skipped[0].first, dir / "broken.pdf");
}

TEST_F(SyncTest, OverlappingRootsDoNotDuplicateFiles) {
    index.add_root(dir / "notes");
    sync(index);
    EXPECT_EQ(index.documents().size(), 3u);
}

TEST_F(SyncTest, MissingRootIsReportedAndItsFilesDropped) {
    sync(index);
    std::filesystem::remove_all(dir / "notes");
    const SyncReport report = sync(index);
    EXPECT_EQ(report.removed, 3u);
    EXPECT_TRUE(index.documents().empty());
}

TEST(IsWithin, ComparesWholePathComponents) {
    EXPECT_TRUE(is_within("/home/me/notes/a.md", "/home/me/notes"));
    EXPECT_TRUE(is_within("/home/me/notes", "/home/me/notes"));
    EXPECT_FALSE(is_within("/home/me/notes-old/a.md", "/home/me/notes"));
    EXPECT_FALSE(is_within("/home/me", "/home/me/notes"));
}

}  // namespace
}  // namespace seek
