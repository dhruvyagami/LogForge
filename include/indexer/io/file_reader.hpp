#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>

namespace indexer::io {

enum class IOStrategy {
    Read,
    Mmap
};

inline std::string to_string(IOStrategy strategy) {
    switch (strategy) {
        case IOStrategy::Read: return "read";
        case IOStrategy::Mmap: return "mmap";
    }
    return "unknown";
}

IOStrategy parse_io_strategy(std::string_view str);

/**
 * @brief Abstract interface for file reading strategies.
 */
class FileReader {
public:
    virtual ~FileReader() = default;

    virtual const std::filesystem::path& path() const noexcept = 0;
    virtual uint64_t file_size() const noexcept = 0;
    virtual IOStrategy strategy() const noexcept = 0;

    /**
     * @brief Stream all content in chunks to the provided consumer callback.
     * 
     * @param on_chunk Invoked with (data_ptr, chunk_size, is_eof).
     *                 For empty files, invoked once with (nullptr, 0, true).
     */
    virtual void read_all(const std::function<void(const char* data, size_t size, bool is_eof)>& on_chunk) = 0;
};

/**
 * @brief Factory method to instantiate the requested FileReader strategy.
 */
std::unique_ptr<FileReader> create_file_reader(const std::filesystem::path& path, 
                                                IOStrategy strategy, 
                                                size_t buffer_size = 64 * 1024);

} // namespace indexer::io
