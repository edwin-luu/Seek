// seek: instant ranked search over your notes and PDFs.

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "seek/extract.hpp"
#include "seek/index.hpp"
#include "seek/search.hpp"
#include "seek/snippet.hpp"
#include "seek/sync.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::string_view kUsage = R"(seek - instant ranked search over your notes and PDFs

Usage:
  seek index [DIR...]      Add folders to the index and read their files.
                           With no folders, refreshes every folder already added.
  seek WORD...             Search. Best matches first, with a highlighted snippet.
                           Every search first picks up new, edited, and deleted files.
  seek "exact phrase"      Quoted words must appear together, in order.
  seek status              Show what is indexed.
  seek forget DIR          Stop indexing a folder.

Options:
  -n, --limit N            Show up to N results (default 10).
  --index FILE             Index file (default: $SEEK_INDEX, else ~/.cache/seek/index.bin).
  --rebuild                With `index`: discard the index and read every file again.
  --no-color               Plain output (also honors NO_COLOR).
  -h, --help               Show this help.
  --version                Show the version.

Use `seek -- index` to search for a word that is also a command name.
Exit status: 0 results found, 1 no results, 2 error.
)";

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Command { Search, Index, Status, Forget, Help, Version };

struct Options {
    Command command = Command::Search;
    std::vector<std::string> args;
    std::size_t limit = 10;
    fs::path index_path;
    bool color = false;
    bool rebuild = false;
};

// ANSI colors that turn themselves off when the output isn't a terminal.
class Style {
public:
    explicit Style(bool enabled) : enabled_(enabled) {}
    std::string bold(std::string_view s) const { return wrap("1", s); }
    std::string dim(std::string_view s) const { return wrap("2", s); }
    std::string match(std::string_view s) const { return wrap("1;33", s); }
    std::string error(std::string_view s) const { return wrap("1;31", s); }

private:
    std::string wrap(std::string_view code, std::string_view s) const {
        return enabled_ ? std::format("\x1b[{}m{}\x1b[0m", code, s) : std::string(s);
    }
    bool enabled_;
};

// ---- small formatting helpers ------------------------------------------------------------

std::string display_path(const fs::path& path) {
    const std::string s = path.string();
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return s;
    const std::string_view h = home;
    if (s == h) return "~";
    if (s.starts_with(h) && s.size() > h.size() && s[h.size()] == '/') return "~" + s.substr(h.size());
    return s;
}

std::string with_commas(std::size_t n) {
    std::string digits = std::to_string(n);
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<std::size_t>(i), ",");
    }
    return digits;
}

std::string count(std::size_t n, std::string_view singular, std::string_view plural) {
    return std::format("{} {}", with_commas(n), n == 1 ? singular : plural);
}

std::string format_duration(Clock::duration d) {
    const double ms = std::chrono::duration<double, std::milli>(d).count();
    if (ms < 10) return std::format("{:.2f} ms", ms);
    if (ms < 1000) return std::format("{:.0f} ms", ms);
    return std::format("{:.1f} s", ms / 1000);
}

std::string format_bytes(std::uintmax_t bytes) {
    const auto b = static_cast<double>(bytes);
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", b / 1024);
    return std::format("{:.1f} MB", b / (1024 * 1024));
}

std::string format_time(std::int64_t unix_seconds) {
    const auto t = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
    localtime_r(&t, &local);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &local);
    return buffer;
}

// Cuts to at most `max` bytes without splitting a UTF-8 character.
std::string truncate(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t cut = max - 3;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "...";
}

// ---- argument parsing --------------------------------------------------------------------

fs::path default_index_path() {
    if (const char* path = std::getenv("SEEK_INDEX"); path && *path) return path;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return fs::path(xdg) / "seek" / "index.bin";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".cache" / "seek" / "index.bin";
    return ".seek-index.bin";
}

std::size_t parse_limit(std::string_view text) {
    std::size_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value == 0) {
        throw UsageError(std::format("invalid result limit '{}'", text));
    }
    return value;
}

Options parse_args(int argc, char** argv) {
    Options options;
    options.index_path = default_index_path();
    options.color = isatty(STDOUT_FILENO) && std::getenv("NO_COLOR") == nullptr;

    std::vector<std::string> positional;
    bool options_done = false;
    bool first_is_literal = false;  // `seek -- index` searches for "index"
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> std::string_view {
            if (i + 1 >= argc) throw UsageError(std::format("{} needs a value", arg));
            return argv[++i];
        };

        if (options_done || arg.size() < 2 || !arg.starts_with('-')) {
            if (positional.empty()) first_is_literal = options_done;
            positional.emplace_back(arg);
        } else if (arg == "--") {
            options_done = true;
        } else if (arg == "-h" || arg == "--help") {
            options.command = Command::Help;
            return options;
        } else if (arg == "--version") {
            options.command = Command::Version;
            return options;
        } else if (arg == "-n" || arg == "--limit") {
            options.limit = parse_limit(value());
        } else if (arg == "--index") {
            options.index_path = value();
        } else if (arg == "--rebuild") {
            options.rebuild = true;
        } else if (arg == "--no-color") {
            options.color = false;
        } else {
            throw UsageError(std::format("unknown option '{}'", arg));
        }
    }

    if (!positional.empty() && !first_is_literal) {
        const std::string& first = positional.front();
        const Command command = first == "index"    ? Command::Index
                                : first == "status" ? Command::Status
                                : first == "forget" ? Command::Forget
                                : first == "help"   ? Command::Help
                                                    : Command::Search;
        if (command != Command::Search) {
            options.command = command;
            positional.erase(positional.begin());
        }
    }
    options.args = std::move(positional);

    if (options.command == Command::Search && options.args.empty()) options.command = Command::Help;
    if (options.rebuild && options.command != Command::Index)
        throw UsageError("--rebuild only works with `seek index`");
    if (options.command == Command::Status && !options.args.empty())
        throw UsageError("`seek status` takes no arguments");
    if (options.command == Command::Forget && options.args.size() != 1)
        throw UsageError("`seek forget` needs exactly one folder");
    return options;
}

// ---- commands ----------------------------------------------------------------------------

fs::path resolve_folder(std::string_view arg) {
    std::error_code ec;
    const fs::path path = fs::canonical(fs::path(arg), ec);
    if (ec || !fs::is_directory(path, ec)) throw UsageError(std::format("'{}' is not a folder", arg));
    return path;
}

// Shows "Reading 12/40  paper.pdf" on one self-erasing line while files are being read.
seek::SyncProgress progress_line() {
    if (!isatty(STDERR_FILENO)) return {};
    return [](std::size_t done, std::size_t total, const fs::path& current) {
        std::cerr << "\r\x1b[2K";
        if (!current.empty())
            std::cerr << std::format("Reading {}/{}  {}", done + 1, total, truncate(current.filename().string(), 60));
        std::cerr << std::flush;
    };
}

int run_index(const Options& options, const Style& style) {
    seek::Index index;
    if (options.rebuild) {
        // Keep the list of folders if the old index is readable; re-read every file either way.
        try {
            const seek::Index old = seek::Index::load(options.index_path);
            for (const fs::path& root : old.roots()) index.add_root(root);
        } catch (const seek::IndexFormatError&) {
        }
    } else {
        index = seek::Index::load(options.index_path);
    }
    for (const std::string& arg : options.args) index.add_root(resolve_folder(arg));
    if (index.roots().empty()) throw UsageError("no folders to index yet. Try: seek index ~/Documents");

    const auto start = Clock::now();
    const seek::SyncReport report = seek::sync(index, progress_line());
    const auto elapsed = Clock::now() - start;
    index.save(options.index_path);

    std::cout << std::format("Indexed {} in {}\n", count(index.roots().size(), "folder", "folders"),
                             format_duration(elapsed));
    std::cout << style.dim(std::format("  {}: {} added, {} updated, {} removed, {} unchanged",
                                       count(index.documents().size(), "file", "files"), report.added, report.updated,
                                       report.removed, report.unchanged))
              << '\n';

    if (!report.skipped.empty()) {
        constexpr std::size_t kShown = 10;
        std::cout << std::format("  Skipped {}:\n", count(report.skipped.size(), "file", "files"));
        for (std::size_t i = 0; i < report.skipped.size() && i < kShown; ++i) {
            const auto& [path, reason] = report.skipped[i];
            std::cout << std::format("    {} {}\n", display_path(path), style.dim("(" + reason + ")"));
        }
        if (report.skipped.size() > kShown)
            std::cout << std::format("    ... and {} more\n", report.skipped.size() - kShown);
    }
    return 0;
}

std::string render(const seek::Snippet& snippet, const Style& style) {
    std::string out;
    std::size_t cursor = 0;
    for (const auto& [begin, end] : snippet.highlights) {
        out.append(snippet.text, cursor, begin - cursor);
        out += style.match(std::string_view(snippet.text).substr(begin, end - begin));
        cursor = end;
    }
    out.append(snippet.text, cursor);
    return out;
}

// The index only stores words and positions, so snippets come from re-reading the file. That
// also tells us when a result is stale (edited or deleted since the last `seek index`).
void print_hit(std::size_t rank, int rank_width, const seek::Document& doc, const std::vector<std::string>& words,
               const Style& style) {
    std::optional<seek::Snippet> snippet;
    std::error_code ec;
    const auto mtime = fs::last_write_time(doc.path, ec);
    const bool readable = !ec;
    const auto size = readable ? fs::file_size(doc.path, ec) : 0;
    const bool unchanged = readable && !ec && size == doc.size && mtime.time_since_epoch().count() == doc.mtime;
    if (unchanged) {
        try {
            snippet = seek::make_snippet(seek::extract_text(doc.path), words);
        } catch (const std::exception&) {
        }
    }

    std::string header = std::format("{:>{}}. {}", rank, rank_width, style.bold(display_path(doc.path)));
    if (snippet && snippet->page > 0) header += style.dim(std::format("  p. {}", snippet->page));
    std::cout << header << '\n';

    const std::string indent(static_cast<std::size_t>(rank_width) + 2, ' ');
    if (!unchanged) {
        std::cout << indent << style.dim("(file changed while searching; search again)") << '\n';
    } else if (snippet && !snippet->text.empty()) {
        std::cout << indent << render(*snippet, style) << '\n';
    }
}

int run_search(const Options& options, const Style& style) {
    seek::Index index = seek::Index::load(options.index_path);
    if (index.roots().empty()) throw UsageError("nothing is indexed yet. Start with: seek index ~/Documents");

    // Refresh first so results always reflect the files as they are now. When nothing changed
    // this only compares timestamps, which takes milliseconds.
    if (seek::sync(index, progress_line()).changed()) index.save(options.index_path);

    const seek::Query query = seek::parse_query(options.args);
    if (query.empty()) throw UsageError("the query has no searchable words");

    const auto start = Clock::now();
    const seek::SearchResult result = seek::search(index, query, options.limit);
    const auto elapsed = Clock::now() - start;

    if (result.hits.empty()) {
        std::cout << std::format("No matches in {}.\n", count(index.documents().size(), "file", "files"));
        return 1;
    }

    const std::vector<std::string> words = query.words();
    const int rank_width = static_cast<int>(std::to_string(result.hits.size()).size());
    for (std::size_t i = 0; i < result.hits.size(); ++i) {
        if (i > 0) std::cout << '\n';
        print_hit(i + 1, rank_width, index.documents()[result.hits[i].doc], words, style);
    }

    const std::string shown = result.hits.size() < result.total
                                  ? std::format("Top {} of {} matches", result.hits.size(), with_commas(result.total))
                                  : count(result.total, "match", "matches");
    std::cout << '\n'
              << style.dim(std::format("{} - ranked {} in {}", shown, count(index.documents().size(), "file", "files"),
                                       format_duration(elapsed)))
              << '\n';
    return 0;
}

int run_status(const Options& options, const Style& style) {
    const seek::Index index = seek::Index::load(options.index_path);
    std::error_code ec;
    const std::uintmax_t file_size = fs::file_size(options.index_path, ec);
    if (ec || index.roots().empty()) {
        std::cout << "Nothing indexed yet. Start with: seek index ~/Documents\n";
        return 0;
    }

    std::cout << std::format("{}   {} {}\n", style.bold("Index"), display_path(options.index_path),
                             style.dim("(" + format_bytes(file_size) + ")"));
    std::cout << std::format("{} {}\n", style.bold("Updated"), format_time(index.updated_at()));
    std::cout << std::format("{} {}, {} distinct words\n", style.bold("Total  "),
                             count(index.documents().size(), "file", "files"), with_commas(index.term_count()));
    std::cout << style.bold("Folders") << '\n';
    for (const fs::path& root : index.roots()) {
        std::size_t files = 0;
        for (const seek::Document& doc : index.documents()) files += seek::is_within(doc.path, root) ? 1u : 0u;
        std::cout << std::format("  {}  {}\n", display_path(root), style.dim(count(files, "file", "files")));
    }
    return 0;
}

int run_forget(const Options& options, const Style& style) {
    seek::Index index = seek::Index::load(options.index_path);
    const fs::path root = fs::weakly_canonical(fs::absolute(options.args.front()));
    if (!index.remove_root(root)) {
        std::string message = std::format("'{}' is not an indexed folder.", options.args.front());
        if (!index.roots().empty()) message += " Indexed folders:";
        for (const fs::path& r : index.roots()) message += "\n  " + display_path(r);
        throw UsageError(message);
    }

    const std::vector<fs::path>& remaining = index.roots();
    const std::size_t removed = index.remove_if([&](const seek::Document& doc) {
        return std::ranges::none_of(remaining, [&](const fs::path& r) { return seek::is_within(doc.path, r); });
    });
    index.save(options.index_path);
    std::cout << std::format("Stopped indexing {} {}\n", display_path(root),
                             style.dim("(" + count(removed, "file", "files") + " removed from the index)"));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Style error_style(isatty(STDERR_FILENO) && std::getenv("NO_COLOR") == nullptr);
    const auto fail = [&](const std::string& message) {
        std::cerr << error_style.error("seek:") << ' ' << message << '\n';
        return 2;
    };

    Options options;
    try {
        options = parse_args(argc, argv);
    } catch (const UsageError& e) {
        return fail(std::string(e.what()) + "\nRun `seek --help` for usage.");
    }

    const Style style(options.color);
    try {
        switch (options.command) {
            case Command::Help: std::cout << kUsage; return 0;
            case Command::Version: std::cout << "seek " << SEEK_VERSION << '\n'; return 0;
            case Command::Index: return run_index(options, style);
            case Command::Status: return run_status(options, style);
            case Command::Forget: return run_forget(options, style);
            case Command::Search: return run_search(options, style);
        }
    } catch (const UsageError& e) {
        return fail(e.what());
    } catch (const seek::IndexFormatError& e) {
        return fail(std::format("the index at {} can't be read ({}).\nRebuild it with: seek index --rebuild DIR...",
                                display_path(options.index_path), e.what()));
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    return 2;
}
