#include <indexer/io/read_reader.hpp>
#include <indexer/io/mmap_reader.hpp>
#include <indexer/io/file_descriptor.hpp>
#include <system_error>
#include <vector>
#include <cstring>
#include <cerrno>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace indexer::io {

IOStrategy parse_io_strategy(std::string_view str) {
    if (str == "read" || str == "READ") {
        return IOStrategy::Read;
    }
    if (str == "mmap" || str == "MMAP") {
        return IOStrategy::Mmap;
    }
    throw std::invalid_argument("Unknown I/O strategy: " + std::string(str) + " (expected 'read' or 'mmap')");
}

std::unique_ptr<FileReader> create_file_reader(const std::filesystem::path& path, 
                                                IOStrategy strategy, 
                                                size_t buffer_size) {
    switch (strategy) {
        case IOStrategy::Read:
            return std::make_unique<ReadReader>(path, buffer_size);
        case IOStrategy::Mmap:
            return std::make_unique<MmapReader>(path);
    }
    throw std::invalid_argument("Unsupported I/O strategy");
}

ReadReader::ReadReader(std::filesystem::path path, size_t buffer_size)
    : path_(std::move(path)), buffer_size_(buffer_size > 0 ? buffer_size : 64 * 1024) {
    
    int flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif

    int fd = ::open(path_.c_str(), flags);
    if (fd < 0) {
        int err = errno;
        throw std::system_error(err, std::generic_category(), 
            "ReadReader failed to open file '" + path_.string() + "': " + std::strerror(err));
    }
    FileDescriptor fd_guard(fd);

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        int err = errno;
        throw std::system_error(err, std::generic_category(), 
            "ReadReader failed to fstat file '" + path_.string() + "': " + std::strerror(err));
    }
    file_size_ = static_cast<uint64_t>(st.st_size);
}

void ReadReader::read_all(const std::function<void(const char* data, size_t size, bool is_eof)>& on_chunk) {
    if (file_size_ == 0) {
        on_chunk(nullptr, 0, true);
        return;
    }

    int flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif

    int fd = ::open(path_.c_str(), flags);
    if (fd < 0) {
        int err = errno;
        throw std::system_error(err, std::generic_category(), 
            "ReadReader failed to open file for reading '" + path_.string() + "': " + std::strerror(err));
    }
    FileDescriptor fd_guard(fd);

    std::vector<char> buffer(buffer_size_);
    uint64_t total_bytes_read = 0;

    while (true) {
        ssize_t bytes_read = 0;
#if defined(_WIN32)
        bytes_read = ::_read(fd, buffer.data(), static_cast<unsigned int>(buffer.size()));
#else
        bytes_read = ::read(fd, buffer.data(), buffer.size());
#endif

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            int err = errno;
            throw std::system_error(err, std::generic_category(), 
                "ReadReader read error on file '" + path_.string() + "': " + std::strerror(err));
        }

        if (bytes_read == 0) {
            // EOF reached
            if (total_bytes_read < file_size_) {
                // If EOF encountered before expected size, signal EOF chunk
                on_chunk(nullptr, 0, true);
            }
            break;
        }

        total_bytes_read += static_cast<uint64_t>(bytes_read);
        bool is_eof = (total_bytes_read >= file_size_);

        on_chunk(buffer.data(), static_cast<size_t>(bytes_read), is_eof);

        if (is_eof) {
            break;
        }
    }
}

} // namespace indexer::io
