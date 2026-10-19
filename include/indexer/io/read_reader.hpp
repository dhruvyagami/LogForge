#pragma once

#include <indexer/io/file_reader.hpp>
#include <indexer/io/file_descriptor.hpp>
#include <vector>

namespace indexer::io {

/**
 * @brief I/O implementation using POSIX read() with a reusable buffer.
 * 
 * Safely handles EINTR, partial reads, and clean EOF termination.
 */
class ReadReader : public FileReader {
public:
    explicit ReadReader(std::filesystem::path path, size_t buffer_size = 64 * 1024);
    ~ReadReader() override = default;

    const std::filesystem::path& path() const noexcept override { return path_; }
    uint64_t file_size() const noexcept override { return file_size_; }
    IOStrategy strategy() const noexcept override { return IOStrategy::Read; }

    void read_all(const std::function<void(const char* data, size_t size, bool is_eof)>& on_chunk) override;

private:
    std::filesystem::path path_;
    size_t buffer_size_;
    uint64_t file_size_{0};
};

} // namespace indexer::io
