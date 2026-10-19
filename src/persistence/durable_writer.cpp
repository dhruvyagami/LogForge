#include <indexer/persistence/durable_writer.hpp>
#include <indexer/persistence/serializer.hpp>
#include <indexer/io/file_descriptor.hpp>
#include <system_error>
#include <atomic>
#include <cstring>
#include <fstream>
#include <cerrno>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace indexer::persistence {

namespace {
std::atomic<uint64_t> g_temp_counter{0};
}

void DurableWriter::write_atomic(const std::filesystem::path& target_path, const WordIndex& index) {
    auto parent_dir = target_path.parent_path();
    if (parent_dir.empty()) {
        parent_dir = ".";
    }

    // Ensure parent directory exists
    std::error_code ec;
    std::filesystem::create_directories(parent_dir, ec);

    // Generate unique temp filename in the same parent directory (same filesystem mount)
    uint64_t id = g_temp_counter.fetch_add(1, std::memory_order_relaxed);
    std::filesystem::path temp_path = parent_dir / (target_path.filename().string() + ".tmp." + 
                                                   std::to_string(::getpid()) + "." + 
                                                   std::to_string(id));

    // Serialize payload to buffer
    std::vector<uint8_t> buffer = Serializer::serialize_to_buffer(index);

    // Open temp file with O_CREAT | O_EXCL | O_WRONLY
    int open_flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
    open_flags |= O_CLOEXEC;
#endif

#if defined(_WIN32)
    open_flags |= _O_BINARY;
    int fd = ::_open(temp_path.string().c_str(), open_flags, _S_IREAD | _S_IWRITE);
#else
    int fd = ::open(temp_path.c_str(), open_flags, 0644);
#endif

    if (fd < 0) {
        int err = errno;
        throw std::system_error(err, std::generic_category(), 
            "DurableWriter failed to create temp file '" + temp_path.string() + "': " + std::strerror(err));
    }
    io::FileDescriptor temp_fd(fd);

    // Write all bytes to temp file handling partial writes and EINTR
    size_t total_written = 0;
    const char* ptr = reinterpret_cast<const char*>(buffer.data());
    size_t remaining = buffer.size();

    while (remaining > 0) {
        ssize_t written = 0;
#if defined(_WIN32)
        written = ::_write(temp_fd.get(), ptr + total_written, static_cast<unsigned int>(remaining));
#else
        written = ::write(temp_fd.get(), ptr + total_written, remaining);
#endif

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            std::filesystem::remove(temp_path, ec);
            throw std::system_error(err, std::generic_category(), 
                "DurableWriter write failed on '" + temp_path.string() + "': " + std::strerror(err));
        }

        total_written += static_cast<size_t>(written);
        remaining -= static_cast<size_t>(written);
    }

    // Fsync the temp file to ensure data is physically written to disk
#if defined(_WIN32)
    HANDLE hFile = reinterpret_cast<HANDLE>(::_get_osfhandle(temp_fd.get()));
    if (hFile != INVALID_HANDLE_VALUE) {
        ::FlushFileBuffers(hFile);
    }
#else
    if (::fsync(temp_fd.get()) != 0) {
        int err = errno;
        std::filesystem::remove(temp_path, ec);
        throw std::system_error(err, std::generic_category(), 
            "DurableWriter fsync failed on temp file '" + temp_path.string() + "': " + std::strerror(err));
    }
#endif

    // Close temp file descriptor before rename
    temp_fd.reset();

    // Atomic rename over target file
#if defined(_WIN32)
    if (!::MoveFileExA(temp_path.string().c_str(), target_path.string().c_str(), 
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = ::GetLastError();
        std::filesystem::remove(temp_path, ec);
        throw std::system_error(err, std::system_category(), 
            "DurableWriter MoveFileEx failed from '" + temp_path.string() + "' to '" + target_path.string() + "'");
    }
#else
    if (::rename(temp_path.c_str(), target_path.c_str()) != 0) {
        int err = errno;
        std::filesystem::remove(temp_path, ec);
        throw std::system_error(err, std::generic_category(), 
            "DurableWriter atomic rename failed from '" + temp_path.string() + "' to '" + target_path.string() + "': " + std::strerror(err));
    }

    // Fsync the parent directory so the directory entry change is durable
    int dir_flags = O_RDONLY;
#if defined(O_DIRECTORY)
    dir_flags |= O_DIRECTORY;
#endif
#if defined(O_CLOEXEC)
    dir_flags |= O_CLOEXEC;
#endif

    int dir_fd = ::open(parent_dir.c_str(), dir_flags);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
#endif
}

WordIndex DurableWriter::read_verified(const std::filesystem::path& target_path) {
    std::ifstream file(target_path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Could not open index file for reading: " + target_path.string());
    }

    std::streamsize size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("Failed to determine file size: " + target_path.string());
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read index data from file: " + target_path.string());
    }

    return Serializer::deserialize_from_buffer(buffer.data(), buffer.size());
}

} // namespace indexer::persistence
