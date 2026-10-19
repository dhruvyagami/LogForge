#include <indexer/tokenizer.hpp>
#include <indexer/index.hpp>
#include <indexer/topk.hpp>
#include <indexer/diff.hpp>
#include <indexer/io/file_reader.hpp>
#include <indexer/concurrency/pipeline.hpp>
#include <indexer/persistence/serializer.hpp>
#include <indexer/persistence/durable_writer.hpp>
#include <indexer/ipc/process_pool.hpp>

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>

namespace {

void print_usage(const char* prog_name) {
    std::cout << "LogForge - High-Performance Concurrent File Indexer for UNIX/Linux\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << prog_name << " index [options] <path...>\n";
    std::cout << "  " << prog_name << " query <index_file> <word>\n";
    std::cout << "  " << prog_name << " topk <index_file> <K>\n";
    std::cout << "  " << prog_name << " diff <old_index_file> <new_index_file>\n\n";
    std::cout << "Options for 'index':\n";
    std::cout << "  --threads=N       Number of concurrent worker threads or processes (default: hardware cores)\n";
    std::cout << "  --io=read|mmap    I/O read strategy (default: mmap)\n";
    std::cout << "  --mode=threads|procs Concurrency model (default: threads)\n";
    std::cout << "  --output=FILE     Persist the resulting index durably to FILE\n";
    std::cout << "  --help, -h        Show this help message\n";
}

int handle_index(int argc, char* argv[]) {
    indexer::concurrency::PipelineConfig pipe_cfg;
    indexer::ipc::ProcessPoolConfig ipc_cfg;
    indexer::ipc::ExecutionMode mode = indexer::ipc::ExecutionMode::Threads;
    std::filesystem::path output_file;
    std::vector<std::filesystem::path> input_paths;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--threads=", 0) == 0) {
            size_t n = std::stoul(arg.substr(10));
            pipe_cfg.num_threads = n;
            ipc_cfg.num_procs = n;
        } else if (arg.rfind("--io=", 0) == 0) {
            auto strat = indexer::io::parse_io_strategy(arg.substr(5));
            pipe_cfg.io_strategy = strat;
            ipc_cfg.io_strategy = strat;
        } else if (arg.rfind("--mode=", 0) == 0) {
            mode = indexer::ipc::parse_execution_mode(arg.substr(7));
        } else if (arg.rfind("--output=", 0) == 0) {
            output_file = arg.substr(9);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        } else {
            input_paths.emplace_back(arg);
        }
    }

    if (input_paths.empty()) {
        std::cerr << "Error: No input files or directories specified.\n";
        return 1;
    }

    std::cout << "Starting LogForge indexer:\n";
    std::cout << "  Mode:       " << indexer::ipc::to_string(mode) << "\n";
    std::cout << "  I/O:        " << indexer::io::to_string(pipe_cfg.io_strategy) << "\n";
    std::cout << "  Concurrency:" << (pipe_cfg.num_threads == 0 ? " auto" : std::to_string(pipe_cfg.num_threads)) << "\n";
    std::cout << "  Scanning " << input_paths.size() << " target path(s)...\n";

    indexer::WordIndex final_index;
    double mib_s = 0.0;
    size_t files_count = 0;
    uint64_t total_bytes = 0;
    double elapsed_sec = 0.0;

    if (mode == indexer::ipc::ExecutionMode::Threads) {
        indexer::concurrency::Pipeline pipeline(pipe_cfg);
        auto res = pipeline.run(input_paths);
        final_index = std::move(res.index);
        mib_s = res.stats.throughput_mib_s;
        files_count = res.stats.files_processed;
        total_bytes = res.stats.total_bytes;
        elapsed_sec = std::chrono::duration<double>(res.stats.duration).count();
    } else {
        ipc_cfg.binary_path = argv[0];
        indexer::ipc::ProcessPool pool(ipc_cfg);
        auto res = pool.run(input_paths);
        final_index = std::move(res.index);
        mib_s = res.throughput_mib_s;
        files_count = res.files_processed;
        total_bytes = res.total_bytes;
        elapsed_sec = std::chrono::duration<double>(res.duration).count();
    }

    std::cout << "\nIndexing Completed Successfully!\n";
    std::cout << "  Files Processed: " << files_count << "\n";
    std::cout << "  Total Volume:    " << std::fixed << std::setprecision(2) 
              << (static_cast<double>(total_bytes) / (1024.0 * 1024.0)) << " MiB\n";
    std::cout << "  Elapsed Time:    " << std::setprecision(3) << elapsed_sec << " s\n";
    std::cout << "  Throughput:      " << std::setprecision(2) << mib_s << " MiB/s\n";
    std::cout << "  Unique Words:    " << final_index.unique_words() << "\n";
    std::cout << "  Total Tokens:    " << final_index.total_words() << "\n";

    if (!output_file.empty()) {
        std::cout << "  Persisting index durably to " << output_file << " ...\n";
        indexer::persistence::DurableWriter::write_atomic(output_file, final_index);
        std::cout << "  Persisted and verified successfully with CRC32 checksum.\n";
    }

    return 0;
}

int handle_query(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " query <index_file> <word>\n";
        return 1;
    }

    std::filesystem::path index_path = argv[2];
    std::string word = argv[3];

    // Canonicalize word
    std::string lower_word;
    for (char c : word) {
        if (indexer::Tokenizer::is_alnum_ascii(c)) {
            lower_word.push_back(indexer::Tokenizer::to_lower_ascii(c));
        }
    }

    try {
        indexer::WordIndex index = indexer::persistence::DurableWriter::read_verified(index_path);
        uint64_t freq = index.get_frequency(lower_word);
        std::cout << "Word: '" << lower_word << "' => Frequency: " << freq << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Query failed: " << e.what() << "\n";
        return 1;
    }
}

int handle_topk(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " topk <index_file> <K>\n";
        return 1;
    }

    std::filesystem::path index_path = argv[2];
    size_t k = std::stoul(argv[3]);

    try {
        indexer::WordIndex index = indexer::persistence::DurableWriter::read_verified(index_path);
        auto top_words = indexer::get_top_k(index, k);

        std::cout << "========================================\n";
        std::cout << "           TOP-" << k << " FREQUENT WORDS       \n";
        std::cout << "========================================\n";
        std::cout << std::left << std::setw(8) << "Rank" 
                  << std::left << std::setw(24) << "Word" 
                  << std::right << std::setw(12) << "Frequency\n";
        std::cout << "----------------------------------------\n";

        for (size_t i = 0; i < top_words.size(); ++i) {
            std::cout << std::left << std::setw(8) << (i + 1)
                      << std::left << std::setw(24) << top_words[i].word
                      << std::right << std::setw(12) << top_words[i].frequency << "\n";
        }
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Top-K query failed: " << e.what() << "\n";
        return 1;
    }
}

int handle_diff(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " diff <old_index_file> <new_index_file>\n";
        return 1;
    }

    std::filesystem::path old_path = argv[2];
    std::filesystem::path new_path = argv[3];

    try {
        indexer::WordIndex old_index = indexer::persistence::DurableWriter::read_verified(old_path);
        indexer::WordIndex new_index = indexer::persistence::DurableWriter::read_verified(new_path);

        indexer::IndexDiff diff = indexer::compute_diff(old_index, new_index);
        diff.print(std::cout);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Diff failed: " << e.what() << "\n";
        return 1;
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    // Check for internal child worker IPC invocation
    if (command == "--worker-ipc") {
        std::filesystem::path manifest_path;
        indexer::io::IOStrategy strategy = indexer::io::IOStrategy::Mmap;

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--manifest=", 0) == 0) {
                manifest_path = arg.substr(11);
            } else if (arg == "--manifest" && i + 1 < argc) {
                manifest_path = argv[++i];
            } else if (arg.rfind("--io=", 0) == 0) {
                strategy = indexer::io::parse_io_strategy(arg.substr(5));
            } else if (arg == "--io" && i + 1 < argc) {
                strategy = indexer::io::parse_io_strategy(argv[++i]);
            }
        }

        return indexer::ipc::ProcessPool::run_worker_mode(manifest_path, strategy);
    }

    if (command == "index") {
        return handle_index(argc, argv);
    } else if (command == "query") {
        return handle_query(argc, argv);
    } else if (command == "topk") {
        return handle_topk(argc, argv);
    } else if (command == "diff") {
        return handle_diff(argc, argv);
    } else if (command == "-h" || command == "--help") {
        print_usage(argv[0]);
        return 0;
    } else {
        std::cerr << "Unknown command: '" << command << "'\n\n";
        print_usage(argv[0]);
        return 1;
    }
}
