#include "test_framework.hpp"
#include <indexer/concurrency/pipeline.hpp>
#include <fstream>

using namespace indexer;
using namespace indexer::concurrency;

namespace {
struct TestDirGuard {
    std::filesystem::path dir;
    TestDirGuard(const std::string& name) {
        dir = std::filesystem::temp_directory_path() / name;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }
    ~TestDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
}

TEST_CASE(PipelineEndToEndIndexing) {
    TestDirGuard dir_guard("logforge_pipeline_test");

    // Create file 1
    {
        std::ofstream f1(dir_guard.dir / "file1.txt");
        f1 << "Concurrency in C++17 is powerful. Concurrency requires discipline.\n";
    }

    // Create file 2 in subfolder
    {
        std::filesystem::create_directories(dir_guard.dir / "sub");
        std::ofstream f2(dir_guard.dir / "sub" / "file2.txt");
        f2 << "Disciplined systems design in C++17.\n";
    }

    PipelineConfig config;
    config.num_threads = 2;
    config.io_strategy = io::IOStrategy::Read;

    Pipeline pipeline(config);
    PipelineResult result = pipeline.run({dir_guard.dir});

    ASSERT_EQ(result.stats.files_processed, 2u);
    ASSERT_TRUE(result.stats.total_bytes > 0);

    // Check frequencies:
    // "concurrency": 2 in file1
    // "in": 1 in file1, 1 in file2 -> 2
    // "c": 1 in file1, 1 in file2 -> 2
    // "17": 1 in file1, 1 in file2 -> 2
    // "powerful": 1
    // "discipline": 1
    // "disciplined": 1
    // "systems": 1
    // "design": 1
    ASSERT_EQ(result.index.get_frequency("concurrency"), 2u);
    ASSERT_EQ(result.index.get_frequency("in"), 2u);
    ASSERT_EQ(result.index.get_frequency("c"), 2u);
    ASSERT_EQ(result.index.get_frequency("17"), 2u);
    ASSERT_EQ(result.index.get_frequency("powerful"), 1u);
    ASSERT_EQ(result.index.get_frequency("discipline"), 1u);
    ASSERT_EQ(result.index.get_frequency("disciplined"), 1u);
    ASSERT_EQ(result.index.get_frequency("systems"), 1u);
    ASSERT_EQ(result.index.get_frequency("design"), 1u);
}
