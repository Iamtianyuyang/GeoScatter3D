#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace gs3d::platform {

/*
 * 跨平台只读内存映射文件 RAII 包装。
 * Windows 下采用 CreateFileW + CreateFileMappingW + MapViewOfFile，
 * POSIX 下采用 open + mmap。
 *
 * 允许多线程无锁并发读取同一个内存映射区间，避免重复系统调用与句柄竞争。
 */
class MemoryMappedFile {
public:
    MemoryMappedFile() = default;
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    [[nodiscard]]
    static std::shared_ptr<MemoryMappedFile> open_read(
        const std::filesystem::path& path
    );

    [[nodiscard]]
    const std::byte* data() const noexcept {
        return data_;
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]]
    bool is_open() const noexcept {
        return data_ != nullptr;
    }

    void close() noexcept;

private:
    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;

#if defined(_WIN32)
    void* file_handle_ = nullptr;
    void* mapping_handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace gs3d::platform
