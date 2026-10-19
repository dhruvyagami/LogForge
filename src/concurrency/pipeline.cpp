#include <indexer/concurrency/pipeline.hpp>
#include <indexer/concurrency/bounded_queue.hpp>
#include <indexer/tokenizer.hpp>
#include <indexer/io/file_reader.hpp>
#include <thread>
#include <atomic>
#include <iostream>

namespace indexer::concurrency {

Pipeline::Pipeline(PipelineConfig config) : config_(config) {
    if (config_.num_threads == 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        config_.num_threads = hw > 0 ? hw : 4;
    }
}

PipelineResult Pipeline::run(const std::vector<std::filesystem::path>& input_paths) {
    auto start_time = std::chrono::steady_clock::now();

    BoundedQueue<std::filesystem::path> queue(config_.queue_capacity);
    std::atomic<size_t> files_processed{0};
    std::atomic<uint64_t> total_bytes{0};

    // Vector to store each worker's local index
    std::vector<WordIndex> worker_indexes(config_.num_threads);

    // Launch worker threads
    std::vector<std::thread> workers;
    workers.reserve(config_.num_threads);

    for (size_t i = 0; i < config_.num_threads; ++i) {
        workers.emplace_back([this, &queue, &worker_indexes, i, &files_processed, &total_bytes]() {
            WordIndex local_index;
            Tokenizer tokenizer;

            while (auto opt_path = queue.pop()) {
                try {
                    auto reader = io::create_file_reader(*opt_path, config_.io_strategy);
                    uint64_t file_sz = reader->file_size();

                    reader->read_all([&tokenizer, &local_index](const char* data, size_t size, bool is_eof) {
                        tokenizer.process_chunk(data, size, is_eof, [&local_index](std::string_view word) {
                            local_index.add_word(word);
                        });
                    });

                    tokenizer.reset();
                    files_processed.fetch_add(1, std::memory_order_relaxed);
                    total_bytes.fetch_add(file_sz, std::memory_order_relaxed);
                } catch (const std::exception& e) {
                    // In a production system, log file read failures without crashing the entire run
                    std::cerr << "[Pipeline Warning] Failed to index " << opt_path->string() << ": " << e.what() << "\n";
                }
            }

            worker_indexes[i] = std::move(local_index);
        });
    }

    // Producer logic: traverse filesystem in dedicated thread
    std::thread producer([&queue, &input_paths]() {
        for (const auto& root : input_paths) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(root, ec)) {
                if (!queue.push(root)) {
                    break;
                }
            } else if (std::filesystem::is_directory(root, ec)) {
                std::filesystem::recursive_directory_iterator it(
                    root, 
                    std::filesystem::directory_options::skip_permission_denied, 
                    ec
                );
                std::filesystem::recursive_directory_iterator end;

                while (it != end && !ec) {
                    if (it->is_regular_file(ec)) {
                        if (!queue.push(it->path())) {
                            break;
                        }
                    }
                    it.increment(ec);
                }
            }
        }
        queue.close();
    });

    // Wait for producer and workers to finish
    if (producer.joinable()) {
        producer.join();
    }

    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }

    // Final merge step: combine worker-local indexes into global index
    WordIndex global_index;
    for (auto& local_idx : worker_indexes) {
        global_index.merge(std::move(local_idx));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    double seconds = std::chrono::duration<double>(elapsed).count();
    double total_mib = static_cast<double>(total_bytes.load()) / (1024.0 * 1024.0);
    double mib_per_sec = seconds > 0.0 ? (total_mib / seconds) : 0.0;

    PipelineResult result;
    result.index = std::move(global_index);
    result.stats.files_processed = files_processed.load();
    result.stats.total_bytes = total_bytes.load();
    result.stats.duration = elapsed;
    result.stats.throughput_mib_s = mib_per_sec;

    return result;
}

} // namespace indexer::concurrency
