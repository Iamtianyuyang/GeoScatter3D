#include "foundation/io/AtomicFile.hpp"

#include <cstdio>
#include <span>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gs3d::foundation {
namespace {

void flush_directory(const std::filesystem::path& dir) {
#ifdef _WIN32
    // Windows has no directory fsync; the rename below is already atomic there.
    (void)dir;
#else
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::fsync(fd); // best effort: a directory fsync failure cannot be repaired here
        ::close(fd);
    }
#endif
}

} // namespace

void atomic_write_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    if (path.empty()) {
        throw std::runtime_error("atomic_write_file: empty path");
    }
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::FILE* file = std::fopen(tmp.string().c_str(), "wb");
        if (file == nullptr) {
            throw std::runtime_error("atomic_write_file: cannot open " + tmp.string());
        }
        const bool written =
            bytes.empty() ||
            std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        const bool flushed = std::fflush(file) == 0;
#ifdef _WIN32
        const bool synced = flushed && ::_commit(::_fileno(file)) == 0;
#else
        const bool synced = flushed && ::fsync(::fileno(file)) == 0;
#endif
        std::fclose(file);
        if (!written || !synced) {
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            throw std::runtime_error("atomic_write_file: write failed for " + tmp.string());
        }
    }
#ifdef _WIN32
    if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        throw std::runtime_error("atomic_write_file: rename failed for " + path.string());
    }
#else
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        throw std::runtime_error("atomic_write_file: rename failed for " + path.string() + ": " +
                                 ec.message());
    }
#endif
    flush_directory(path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path());
}

void atomic_write_file(const std::filesystem::path& path, std::string_view text) {
    atomic_write_file(path, std::as_bytes(std::span(text.data(), text.size())));
}

} // namespace gs3d::foundation
