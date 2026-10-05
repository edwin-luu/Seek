#include "seek/extract.hpp"

#include <poppler-document.h>
#include <poppler-global.h>
#include <poppler-page.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>

namespace seek {
namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxTextFileBytes = std::uintmax_t{64} << 20;
constexpr std::array<std::string_view, 3> kTextExtensions{".txt", ".md", ".markdown"};

std::string lowercase_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(),
                           [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
    return ext;
}

// Poppler prints parser warnings for slightly malformed PDFs straight to stderr, which would
// garble seek's output. We report failures ourselves, so mute it once per process.
void silence_poppler() {
    static std::once_flag once;
    std::call_once(once, [] { poppler::set_debug_error_function([](const std::string&, void*) {}, nullptr); });
}

ExtractedText read_text_file(const fs::path& path) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) throw ExtractError("cannot read file: " + ec.message());
    if (size > kMaxTextFileBytes) throw ExtractError("text file is larger than 64 MiB");

    std::ifstream in(path, std::ios::binary);
    if (!in) throw ExtractError("cannot open file");

    ExtractedText out;
    out.text.resize(static_cast<std::size_t>(size));
    in.read(out.text.data(), static_cast<std::streamsize>(size));
    out.text.resize(static_cast<std::size_t>(in.gcount()));
    return out;
}

ExtractedText read_pdf(const fs::path& path) {
    silence_poppler();

    // Poppler hands back raw owning pointers; unique_ptr frees them on every exit path.
    const std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(path.string()));
    if (!doc) throw ExtractError("not a readable PDF");
    if (doc->is_locked()) throw ExtractError("PDF is password-protected");

    ExtractedText out;
    const int pages = doc->pages();
    for (int i = 0; i < pages; ++i) {
        out.page_starts.push_back(out.text.size());
        const std::unique_ptr<poppler::page> page(doc->create_page(i));
        if (page) {
            const poppler::byte_array utf8 = page->text().to_utf8();
            out.text.append(utf8.data(), utf8.size());
        }
        out.text += '\f';
    }
    return out;
}

}  // namespace

int ExtractedText::page_at(std::size_t offset) const {
    // page_starts is ascending, so the page is the number of starts at or before `offset`.
    return static_cast<int>(std::ranges::upper_bound(page_starts, offset) - page_starts.begin());
}

bool is_supported(const fs::path& path) {
    const std::string ext = lowercase_extension(path);
    return ext == ".pdf" || std::ranges::find(kTextExtensions, ext) != kTextExtensions.end();
}

ExtractedText extract_text(const fs::path& path) {
    return lowercase_extension(path) == ".pdf" ? read_pdf(path) : read_text_file(path);
}

}  // namespace seek
