#include "test_framework.hpp"
#include <indexer/io/file_reader.hpp>
#include <indexer/io/read_reader.hpp>
#include <indexer/io/mmap_reader.hpp>
#include <fstream>
#include <system_error>

using namespace indexer::io;

namespace {
struct TempFileGuard {
    std::filesystem::path path;
    TempFileGuard(const std::string& name, const std::string& content) {
        path = std::filesystem::temp_directory_path() / name;
        std::ofstream out(path, std::ios::binary);
        out.write(content.data(), content.size());
    }
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};
}

TEST_CASE(IOReadReaderNormalFile) {
    std::string content = "The quick brown fox jumps over the lazy dog.";
    TempFileGuard guard("logforge_test_io_read.txt", content);

    ReadReader reader(guard.path, 16); // small 16-byte buffer
    ASSERT_EQ(reader.file_size(), content.size());

    std::string collected;
    reader.read_all([&collected](const char* data, size_t size, bool /*is_eof*/) {
        if (data && size > 0) {
            collected.append(data, size);
        }
    });

    ASSERT_EQ(collected, content);
}

TEST_CASE(IOMmapReaderNormalFile) {
    std::string content = "Mmap sequential reading test with sample text data.";
    TempFileGuard guard("logforge_test_io_mmap.txt", content);

    MmapReader reader(guard.path);
    ASSERT_EQ(reader.file_size(), content.size());

    std::string collected;
    reader.read_all([&collected](const char* data, size_t size, bool is_eof) {
        if (data && size > 0) {
            collected.append(data, size);
        }
        ASSERT_TRUE(is_eof);
    });

    ASSERT_EQ(collected, content);
}

TEST_CASE(IOEmptyFileHandling) {
    TempFileGuard guard("logforge_test_empty.txt", "");

    // ReadReader on empty file
    ReadReader read_reader(guard.path);
    ASSERT_EQ(read_reader.file_size(), 0u);
    bool read_eof_called = false;
    read_reader.read_all([&read_eof_called](const char* data, size_t size, bool is_eof) {
        ASSERT_EQ(data, nullptr);
        ASSERT_EQ(size, 0u);
        ASSERT_TRUE(is_eof);
        read_eof_called = true;
    });
    ASSERT_TRUE(read_eof_called);

    // MmapReader on empty file
    MmapReader mmap_reader(guard.path);
    ASSERT_EQ(mmap_reader.file_size(), 0u);
    bool mmap_eof_called = false;
    mmap_reader.read_all([&mmap_eof_called](const char* data, size_t size, bool is_eof) {
        ASSERT_EQ(data, nullptr);
        ASSERT_EQ(size, 0u);
        ASSERT_TRUE(is_eof);
        mmap_eof_called = true;
    });
    ASSERT_TRUE(mmap_eof_called);
}

TEST_CASE(IONonExistentFileThrows) {
    ASSERT_THROWS(ReadReader("this_file_definitely_does_not_exist_xyz123.bin"), std::system_error);
    ASSERT_THROWS(MmapReader("this_file_definitely_does_not_exist_xyz123.bin"), std::system_error);
}
