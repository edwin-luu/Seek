#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "seek/index.hpp"

namespace seek {

struct SyncReport {
    std::size_t added = 0;
    std::size_t updated = 0;
    std::size_t removed = 0;
    std::size_t unchanged = 0;
    std::vector<std::pair<std::filesystem::path, std::string>> skipped;  // path, reason

    bool changed() const { return added + updated + removed > 0; }
};

// Called before each file is read (done < total), and once at the end with an empty path.
using SyncProgress = std::function<void(std::size_t done, std::size_t total, const std::filesystem::path& current)>;

// Brings the index in line with the files under its roots. Only new or changed files are read;
// unchanged files (same modification time and size) are kept as-is, and deleted files are dropped.
// Hidden files and folders (".git", ".cache", ...) and node_modules are skipped.
SyncReport sync(Index& index, const SyncProgress& progress = {});

// True if `path` is `root` itself or somewhere inside it.
bool is_within(const std::filesystem::path& path, const std::filesystem::path& root);

}  // namespace seek
