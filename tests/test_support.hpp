#pragma once

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace seek::test {

// A fresh folder for one test, deleted automatically when the test ends (RAII).
class TempDir {
public:
    TempDir() {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() / std::format("seek-test-{:x}", random());
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(std::string_view name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

inline void write_file(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

// Makes a file look modified even if the test runs within the filesystem's timestamp resolution.
inline void bump_mtime(const std::filesystem::path& path) {
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
}

// Writes a minimal valid PDF with one line of Helvetica text per page. Generating it in code
// keeps binary fixtures out of the repo. Page text must not contain '(', ')' or '\'.
inline void write_pdf(const std::filesystem::path& path, const std::vector<std::string>& pages) {
    std::vector<std::string> objects;
    std::string kids;
    for (std::size_t i = 0; i < pages.size(); ++i) kids += std::format("{} 0 R ", 4 + 2 * i);
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back(std::format("<< /Type /Pages /Kids [{}] /Count {} >>", kids, pages.size()));
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const std::string stream = std::format("BT /F1 12 Tf 72 720 Td ({}) Tj ET", pages[i]);
        objects.push_back(
            std::format("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> "
                        "/Contents {} 0 R >>",
                        5 + 2 * i));
        objects.push_back(std::format("<< /Length {} >>\nstream\n{}\nendstream", stream.size(), stream));
    }

    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::format("{} 0 obj\n{}\nendobj\n", i + 1, objects[i]);
    }
    const std::size_t xref = pdf.size();
    pdf += std::format("xref\n0 {}\n0000000000 65535 f \n", objects.size() + 1);
    for (const std::size_t offset : offsets) pdf += std::format("{:010} 00000 n \n", offset);
    pdf += std::format("trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n", objects.size() + 1, xref);
    write_file(path, pdf);
}

}  // namespace seek::test
