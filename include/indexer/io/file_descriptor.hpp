#pragma once

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace indexer::io {

/**
 * @brief RAII wrapper for POSIX file descriptors.
 * 
 * Ensures file descriptors are deterministically closed without resource leaks.
 */
class FileDescriptor {
public:
    constexpr FileDescriptor() noexcept : fd_(-1) {}
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}

    ~FileDescriptor() {
        reset();
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    bool is_valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return is_valid(); }

    int release() noexcept {
        int temp = fd_;
        fd_ = -1;
        return temp;
    }

    void reset(int new_fd = -1) noexcept {
        if (fd_ >= 0) {
#if defined(_WIN32)
            ::_close(fd_);
#else
            ::close(fd_);
#endif
        }
        fd_ = new_fd;
    }

private:
    int fd_{-1};
};

} // namespace indexer::io
