#include <indexer/concurrency/pipeline.hpp>
#include <indexer/io/file_reader.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>

namespace {

void generate_synthetic_corpus(const std::filesystem::path& dir, size_t target_size_bytes, size_t files_count) {
    std::cout << "[Benchmark Setup] Generating synthetic corpus at " << dir.string() 
              << " (~" << (target_size_bytes / (1024 * 1024)) << " MiB across " 
              << files_count << " files)...\n";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Realistic vocabulary with varied lengths
    const std::vector<std::string> vocab = {
        "the", "of", "and", "a", "to", "in", "is", "you", "that", "it",
        "he", "was", "for", "on", "are", "as", "with", "his", "they", "i",
        "at", "be", "this", "have", "from", "or", "one", "had", "by", "word",
        "but", "not", "what", "all", "were", "we", "when", "your", "can", "said",
        "there", "use", "an", "each", "which", "she", "do", "how", "their", "if",
        "pipeline", "concurrency", "indexer", "posix", "mmap", "syscall", "kernel",
        "buffer", "throughput", "latency", "benchmark", "thread", "mutex", "storage"
    };

    std::mt19937 rng(42);
    // Zipfian-like distribution (heavier weight on earlier words)
    std::discrete_distribution<size_t> dist({
        100, 80, 75, 70, 65, 60, 55, 50, 45, 40,
        35, 30, 28, 26, 24, 22, 20, 18, 16, 14,
        12, 11, 10, 9, 8, 7, 6, 5, 4, 3,
        3, 3, 3, 3, 3, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    });

    size_t bytes_per_file = target_size_bytes / files_count;

    for (size_t f = 0; f < files_count; ++f) {
        auto file_path = dir / ("corpus_shard_" + std::to_string(f) + ".txt");
        std::ofstream out(file_path, std::ios::binary);

        size_t written = 0;
        while (written < bytes_per_file) {
            const std::string& word = vocab[dist(rng)];
            out << word << " ";
            written += word.size() + 1;
        }
    }
    std::cout << "[Benchmark Setup] Corpus generation complete.\n\n";
}

struct RunMetrics {
    double duration_s{0.0};
    double throughput_mib_s{0.0};
    uint64_t total_tokens{0};
    size_t unique_words{0};
};

RunMetrics execute_run(const std::filesystem::path& dir, size_t threads, indexer::io::IOStrategy strategy) {
    indexer::concurrency::PipelineConfig config;
    config.num_threads = threads;
    config.io_strategy = strategy;

    indexer::concurrency::Pipeline pipeline(config);
    auto res = pipeline.run({dir});

    RunMetrics m;
    m.duration_s = std::chrono::duration<double>(res.stats.duration).count();
    m.throughput_mib_s = res.stats.throughput_mib_s;
    m.total_tokens = res.index.total_words();
    m.unique_words = res.index.unique_words();
    return m;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::cout << "========================================================================\n";
    std::cout << "               LOGFORGE SYSTEMS PERFORMANCE BENCHMARK                   \n";
    std::cout << "========================================================================\n\n";

    size_t corpus_size_mib = 128; // Default 128 MiB for quick local run; configurable up to 4 GiB
    size_t files_count = 64;

    if (argc >= 2) {
        corpus_size_mib = std::stoul(argv[1]);
    }
    if (argc >= 3) {
        files_count = std::stoul(argv[2]);
    }

    std::filesystem::path bench_dir = std::filesystem::temp_directory_path() / "logforge_benchmark_corpus";
    std::error_code ec;
    std::filesystem::remove_all(bench_dir, ec);

    generate_synthetic_corpus(bench_dir, corpus_size_mib * 1024 * 1024, files_count);

    // Warm-up run (to bring pages into OS page cache if desired)
    std::cout << "[Warm-up] Executing warm-up run...\n";
    execute_run(bench_dir, 1, indexer::io::IOStrategy::Mmap);

    // 1. Throughput comparison: read() vs mmap() at 1 thread
    std::cout << "\n------------------------------------------------------------------------\n";
    std::cout << " 1. I/O STRATEGY COMPARISON (Single-threaded, Warm Cache)\n";
    std::cout << "------------------------------------------------------------------------\n";
    
    auto metrics_read = execute_run(bench_dir, 1, indexer::io::IOStrategy::Read);
    auto metrics_mmap = execute_run(bench_dir, 1, indexer::io::IOStrategy::Mmap);

    std::cout << std::left << std::setw(16) << "Strategy"
              << std::right << std::setw(14) << "Time (s)"
              << std::right << std::setw(20) << "Throughput (MiB/s)"
              << std::right << std::setw(16) << "Total Tokens\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(16) << "POSIX read()"
              << std::right << std::setw(14) << std::fixed << std::setprecision(3) << metrics_read.duration_s
              << std::right << std::setw(20) << std::fixed << std::setprecision(2) << metrics_read.throughput_mib_s
              << std::right << std::setw(16) << metrics_read.total_tokens << "\n";
    std::cout << std::left << std::setw(16) << "POSIX mmap()"
              << std::right << std::setw(14) << std::fixed << std::setprecision(3) << metrics_mmap.duration_s
              << std::right << std::setw(20) << std::fixed << std::setprecision(2) << metrics_mmap.throughput_mib_s
              << std::right << std::setw(16) << metrics_mmap.total_tokens << "\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << "Note on Cold Cache: On cold runs (e.g., after echo 3 > /proc/sys/vm/drop_caches),\n";
    std::cout << "mmap suffers page faults on touch, whereas read() initiates larger readahead.\n";
    std::cout << "On warm cache, mmap eliminates syscall overhead and user-kernel buffer copying.\n\n";

    // 2. Multithreaded scaling benchmark: 1, 2, 4, 8 threads
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << " 2. MULTITHREADED SPEEDUP SCALING (mmap I/O)\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(10) << "Threads"
              << std::right << std::setw(14) << "Time (s)"
              << std::right << std::setw(20) << "Throughput (MiB/s)"
              << std::right << std::setw(16) << "Speedup vs 1T\n";
    std::cout << "------------------------------------------------------------------------\n";

    const std::vector<size_t> thread_counts = {1, 2, 4, 8};
    double baseline_time = 0.0;

    for (size_t t : thread_counts) {
        auto m = execute_run(bench_dir, t, indexer::io::IOStrategy::Mmap);
        if (t == 1) {
            baseline_time = m.duration_s;
        }
        double speedup = (baseline_time > 0.0 && m.duration_s > 0.0) ? (baseline_time / m.duration_s) : 1.0;

        std::cout << std::left << std::setw(10) << t
                  << std::right << std::setw(14) << std::fixed << std::setprecision(3) << m.duration_s
                  << std::right << std::setw(20) << std::fixed << std::setprecision(2) << m.throughput_mib_s
                  << std::right << std::setw(15) << std::fixed << std::setprecision(2) << speedup << "x\n";
    }
    std::cout << "------------------------------------------------------------------------\n\n";

    // Cleanup synthetic corpus
    std::filesystem::remove_all(bench_dir, ec);
    std::cout << "[Benchmark Cleanup] Temporary benchmark corpus removed.\n";
    return 0;
}
