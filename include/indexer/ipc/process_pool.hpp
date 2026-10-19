#pragma once

#include <indexer/index.hpp>
#include <indexer/io/file_reader.hpp>
#include <vector>
#include <filesystem>
#include <chrono>

namespace indexer::ipc {

enum class ExecutionMode {
    Threads,
    Procs
};

inline std::string to_string(ExecutionMode mode) {
    switch (mode) {
        case ExecutionMode::Threads: return "threads";
        case ExecutionMode::Procs: return "procs";
    }
    return "unknown";
}

ExecutionMode parse_execution_mode(std::string_view str);

struct ProcessPoolConfig {
    size_t num_procs{0}; // 0 = hardware concurrency
    io::IOStrategy io_strategy{io::IOStrategy::Mmap};
    std::filesystem::path binary_path;
};

struct ProcessPoolResult {
    WordIndex index;
    size_t files_processed{0};
    uint64_t total_bytes{0};
    std::chrono::nanoseconds duration{0};
    double throughput_mib_s{0.0};
};

/**
 * @brief Multiprocess indexer using UNIX fork/exec and pipe IPC.
 * 
 * Partitions input file work across N child processes. Each child executes the
 * indexer binary in worker mode, streams its serialized partial index back across
 * a pipe, and the parent reads, validates, merges, and reaps children via waitpid.
 */
class ProcessPool {
public:
    explicit ProcessPool(ProcessPoolConfig config);
    ~ProcessPool() = default;

    /**
     * @brief Execute multiprocess indexing on input file paths.
     */
    ProcessPoolResult run(const std::vector<std::filesystem::path>& input_paths);

    /**
     * @brief Worker routine executed by children launched via fork/exec.
     * 
     * Reads file paths from manifest, indexes them, serializes the partial index
     * to standard output (pipe), and exits.
     */
    static int run_worker_mode(const std::filesystem::path& manifest_path, io::IOStrategy strategy);

private:
    ProcessPoolConfig config_;
};

} // namespace indexer::ipc
