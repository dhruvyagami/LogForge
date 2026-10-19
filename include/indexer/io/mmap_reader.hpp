#pragma once

#include <indexer/io/file_reader.hpp>
#include <indexer/io/file_descriptor.hpp>

namespace indexer::io {

/**
 * @brief I/O implementation using POSIX mmap() with madvise(MADV_SEQUENTIAL).
 * 
 * Maps the target file into virtual address space, advises sequential access,
 * and guarantees munmap() RAII cleanup upon destruction.
 */
class MmapReader : public FileReader {
public:
    explicit MmapReader(std::filesystem::path path);
    ~MmapReader() override;

    MmapReader(const MmapReader&) = delete;
    MmapReader& operator=(const MmapReader&) = delete;
    MmapReader(MmapReader&& other) noexcept;
    MmapReader& operator=(MmapReader&& other) noexcept;

    const std::filesystem::path& path() const noexcept override { return path_; }
    uint64_t file_size() const noexcept override { return file_size_; }
    IOStrategy strategy() const noexcept override { return IOStrategy::Mmap; }

    void read_all(const std::function<void(const char* data, size_t size, bool is_eof)>& on_chunk) override;

private:
    void cleanup() noexcept;

    std::filesystem::path path_;
    uint64_t file_size_{0};
    void* mapped_data_{nullptr};
};

} // namespace indexer::io
