#include <indexer/io/mmap_reader.hpp>
#include <indexer/io/file_descriptor.hpp>
#include <system_error>
#include <cstring>
#include <cerrno>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace indexer::io {

MmapReader::MmapReader(std::filesystem::path path)
    : path_(std::move(path)) {
    
    int flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif

    int fd = ::open(path_.c_str(), flags);
    if (fd < 0) {
        int err = errno;
        throw std::system_error(err, std::generic_category(), 
            "MmapReader failed to open file '" + path_.string() + "': " + std::strerror(err));
    }
    FileDescriptor fd_guard(fd);

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        int err = errno;
        throw std::system_error(err, std::generic_category(), 
            "MmapReader failed to fstat file '" + path_.string() + "': " + std::strerror(err));
    }
    file_size_ = static_cast<uint64_t>(st.st_size);

    // Empty files cannot be mmap'd (mmap returns EINVAL on size 0)
    if (file_size_ == 0) {
        mapped_data_ = nullptr;
        return;
    }

#if defined(_WIN32)
    HANDLE hFile = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to get OS file handle on " + path_.string());
    }
    HANDLE hMapping = ::CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMapping) {
        throw std::system_error(GetLastError(), std::system_category(), "CreateFileMapping failed on " + path_.string());
    }
    mapped_data_ = ::MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, file_size_);
    ::CloseHandle(hMapping);
    if (!mapped_data_) {
        throw std::system_error(GetLastError(), std::system_category(), "MapViewOfFile failed on " + path_.string());
    }
#else
    void* addr = ::mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        int err = errno;
        throw std::system_error(err, std::generic_category(), 
            "MmapReader mmap() failed on '" + path_.string() + "': " + std::strerror(err));
    }
    mapped_data_ = addr;

    // Advise sequential memory access pattern to kernel
    if (::madvise(mapped_data_, file_size_, MADV_SEQUENTIAL) != 0) {
        // Non-fatal advisory warning; continue execution
    }
#endif
}

MmapReader::~MmapReader() {
    cleanup();
}

MmapReader::MmapReader(MmapReader&& other) noexcept
    : path_(std::move(other.path_)),
      file_size_(other.file_size_),
      mapped_data_(other.mapped_data_) {
    other.file_size_ = 0;
    other.mapped_data_ = nullptr;
}

MmapReader& MmapReader::operator=(MmapReader&& other) noexcept {
    if (this != &other) {
        cleanup();
        path_ = std::move(other.path_);
        file_size_ = other.file_size_;
        mapped_data_ = other.mapped_data_;
        other.file_size_ = 0;
        other.mapped_data_ = nullptr;
    }
    return *this;
}

void MmapReader::cleanup() noexcept {
    if (mapped_data_ != nullptr) {
#if defined(_WIN32)
        ::UnmapViewOfFile(mapped_data_);
#else
        ::munmap(mapped_data_, file_size_);
#endif
        mapped_data_ = nullptr;
    }
}

void MmapReader::read_all(const std::function<void(const char* data, size_t size, bool is_eof)>& on_chunk) {
    if (file_size_ == 0 || mapped_data_ == nullptr) {
        on_chunk(nullptr, 0, true);
        return;
    }

    // Entire mmapped region is passed as a single contiguous slice with EOF=true
    on_chunk(static_cast<const char*>(mapped_data_), file_size_, true);
}

} // namespace indexer::io
