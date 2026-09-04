#include "platform/MemoryMappedFile.hpp"

#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gs3d::platform {

MemoryMappedFile::~MemoryMappedFile() {
    close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : data_(other.data_)
    , size_(other.size_)
#if defined(_WIN32)
    , file_handle_(other.file_handle_)
    , mapping_handle_(other.mapping_handle_)
#else
    , fd_(other.fd_)
#endif
{
    other.data_ = nullptr;
    other.size_ = 0;
#if defined(_WIN32)
    other.file_handle_ = nullptr;
    other.mapping_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
#if defined(_WIN32)
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_ = nullptr;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

std::shared_ptr<MemoryMappedFile> MemoryMappedFile::open_read(
    const std::filesystem::path& path
) {
    if (path.empty()) {
        return nullptr;
    }

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec || file_size == 0) {
        return nullptr;
    }

    auto instance = std::make_shared<MemoryMappedFile>();

#if defined(_WIN32)
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    HANDLE mapping = CreateFileMappingW(
        file,
        nullptr,
        PAGE_READONLY,
        0,
        0,
        nullptr
    );

    if (!mapping) {
        CloseHandle(file);
        return nullptr;
    }

    void* view = MapViewOfFile(
        mapping,
        FILE_MAP_READ,
        0,
        0,
        0
    );

    if (!view) {
        CloseHandle(mapping);
        CloseHandle(file);
        return nullptr;
    }

    instance->data_ = static_cast<const std::byte*>(view);
    instance->size_ = static_cast<std::size_t>(file_size);
    instance->file_handle_ = file;
    instance->mapping_handle_ = mapping;

#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return nullptr;
    }

    void* addr = ::mmap(
        nullptr,
        static_cast<std::size_t>(file_size),
        PROT_READ,
        MAP_SHARED,
        fd,
        0
    );

    if (addr == MAP_FAILED) {
        ::close(fd);
        return nullptr;
    }

    instance->data_ = static_cast<const std::byte*>(addr);
    instance->size_ = static_cast<std::size_t>(file_size);
    instance->fd_ = fd;
#endif

    return instance;
}

void MemoryMappedFile::close() noexcept {
    if (data_) {
#if defined(_WIN32)
        UnmapViewOfFile(data_);
        data_ = nullptr;
        if (mapping_handle_) {
            CloseHandle(static_cast<HANDLE>(mapping_handle_));
            mapping_handle_ = nullptr;
        }
        if (file_handle_ && file_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(static_cast<HANDLE>(file_handle_));
            file_handle_ = nullptr;
        }
#else
        ::munmap(const_cast<std::byte*>(data_), size_);
        data_ = nullptr;
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }
    size_ = 0;
}

} // namespace gs3d::platform
