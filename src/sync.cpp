#include "seek/sync.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "seek/extract.hpp"

namespace seek {
namespace fs = std::filesystem;

namespace {

struct FileStamp {
    std::int64_t mtime = 0;
    std::uint64_t size = 0;
    bool operator==(const FileStamp&) const = default;
};

bool is_ignored(const fs::path& path) {
    const std::string name = path.filename().string();
    return name.starts_with('.') || name == "node_modules" || name == "__pycache__";
}

// Every supported file under the roots, with its current stamp. A std::map keyed by path also
// de-duplicates files reachable from two overlapping roots (e.g. ~/notes and ~).
std::map<fs::path, FileStamp> scan(const std::vector<fs::path>& roots, SyncReport& report) {
    std::map<fs::path, FileStamp> files;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            report.skipped.emplace_back(root, "folder not found");
            continue;
        }

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            report.skipped.emplace_back(root, ec.message());
            continue;
        }
        for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
            if (ec) {
                report.skipped.emplace_back(root, ec.message());
                break;
            }
            const fs::directory_entry& entry = *it;
            if (is_ignored(entry.path())) {
                if (entry.is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file(ec) || !is_supported(entry.path())) continue;

            const auto mtime = entry.last_write_time(ec);
            if (ec) continue;
            const std::uintmax_t size = entry.file_size(ec);
            if (ec) continue;
            files[entry.path()] = {static_cast<std::int64_t>(mtime.time_since_epoch().count()), size};
        }
    }
    return files;
}

}  // namespace

SyncReport sync(Index& index, const SyncProgress& progress) {
    SyncReport report;
    std::map<fs::path, FileStamp> files = scan(index.roots(), report);

    // Compare what's indexed with what's on disk. Afterwards `files` holds only the paths that
    // need reading: new files and changed ones.
    std::set<fs::path> stale;
    for (const Document& doc : index.documents()) {
        const auto it = files.find(doc.path);
        if (it == files.end()) {
            stale.insert(doc.path);
            ++report.removed;
        } else if (it->second != FileStamp{doc.mtime, doc.size}) {
            stale.insert(doc.path);
        } else {
            ++report.unchanged;
            files.erase(it);
        }
    }
    index.remove_if([&](const Document& doc) { return stale.contains(doc.path); });

    std::size_t done = 0;
    for (const auto& [path, stamp] : files) {
        if (progress) progress(done, files.size(), path);
        const bool was_indexed = stale.contains(path);
        try {
            const ExtractedText extracted = extract_text(path);
            index.add(Document{path, stamp.mtime, stamp.size, 0}, extracted.text);
            ++(was_indexed ? report.updated : report.added);
        } catch (const std::exception& e) {
            report.skipped.emplace_back(path, e.what());
            if (was_indexed) ++report.removed;
        }
        ++done;
    }
    if (progress) progress(done, files.size(), {});

    index.mark_updated();
    return report;
}

bool is_within(const fs::path& path, const fs::path& root) {
    const auto [root_end, path_it] = std::mismatch(root.begin(), root.end(), path.begin(), path.end());
    return root_end == root.end();
}

}  // namespace seek
