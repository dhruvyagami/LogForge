#pragma once

#include <indexer/index.hpp>
#include <indexer/io/file_reader.hpp>
#include <vector>
#include <filesystem>
#include <chrono>

namespace indexer::concurrency {

struct PipelineConfig {
    size_t num_threads{0}; // 0 = auto-detect hardware concurrency
    io::IOStrategy io_strategy{io::IOStrategy::Mmap};
    size_t queue_capacity{1024};
};

struct PipelineStats {
    size_t files_processed{0};
    uint64_t total_bytes{0};
    std::chrono::nanoseconds duration{0};
    double throughput_mib_s{0.0};
};

struct PipelineResult {
    WordIndex index;
    PipelineStats stats;
};

/**
 * @brief Multithreaded producer-consumer indexing pipeline.
 * 
 * Producer thread traverses the filesystem pushing file paths into a bounded queue.
 * N worker threads pop file paths, execute chunked tokenization into thread-local
 * indexes (zero locks on hot path), and a final reduction combines them into a
 * single global index.
 */
class Pipeline {
public:
    explicit Pipeline(PipelineConfig config);
    ~Pipeline() = default;

    /**
     * @brief Execute the pipeline on the given input roots (files and directories).
     * 
     * @param input_paths List of root file or directory paths to index.
     * @return PipelineResult Containing the merged WordIndex and performance metrics.
     */
    PipelineResult run(const std::vector<std::filesystem::path>& input_paths);

private:
    PipelineConfig config_;
};

} // namespace indexer::concurrency
