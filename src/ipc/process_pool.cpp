#include <indexer/ipc/process_pool.hpp>
#include <indexer/persistence/serializer.hpp>
#include <indexer/tokenizer.hpp>
#include <indexer/io/file_descriptor.hpp>
#include <system_error>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cerrno>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <csignal>
#endif

namespace indexer::ipc {

ExecutionMode parse_execution_mode(std::string_view str) {
    if (str == "threads" || str == "THREADS") {
        return ExecutionMode::Threads;
    }
    if (str == "procs" || str == "PROCS") {
        return ExecutionMode::Procs;
    }
    throw std::invalid_argument("Unknown execution mode: " + std::string(str) + " (expected 'threads' or 'procs')");
}

ProcessPool::ProcessPool(ProcessPoolConfig config) : config_(std::move(config)) {
    if (config_.num_procs == 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        config_.num_procs = hw > 0 ? hw : 4;
    }

    if (config_.binary_path.empty()) {
#if defined(__linux__)
        // Auto-detect own executable path via /proc/self/exe on Linux
        char buf[PATH_MAX];
        ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            config_.binary_path = buf;
        } else {
            config_.binary_path = "indexer";
        }
#else
        config_.binary_path = "indexer";
#endif
    }
}

int ProcessPool::run_worker_mode(const std::filesystem::path& manifest_path, io::IOStrategy strategy) {
    std::ifstream manifest_file(manifest_path);
    if (!manifest_file) {
        std::cerr << "[Worker IPC Error] Could not open manifest: " << manifest_path << "\n";
        return 1;
    }

    WordIndex local_index;
    Tokenizer tokenizer;
    std::string line;

    while (std::getline(manifest_file, line)) {
        if (line.empty()) continue;
        std::filesystem::path file_path(line);

        try {
            auto reader = io::create_file_reader(file_path, strategy);
            reader->read_all([&tokenizer, &local_index](const char* data, size_t size, bool is_eof) {
                tokenizer.process_chunk(data, size, is_eof, [&local_index](std::string_view word) {
                    local_index.add_word(word);
                });
            });
            tokenizer.reset();
        } catch (const std::exception& e) {
            std::cerr << "[Worker IPC Warning] Failed reading " << file_path << ": " << e.what() << "\n";
        }
    }

    // Serialize index into buffer
    std::vector<uint8_t> buffer = persistence::Serializer::serialize_to_buffer(local_index);

    // Stream buffer directly to stdout (the IPC pipe)
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    const char* ptr = reinterpret_cast<const char*>(buffer.data());
    size_t remaining = buffer.size();

    while (remaining > 0) {
        ssize_t written = 0;
#if defined(_WIN32)
        written = ::_write(_fileno(stdout), ptr, static_cast<unsigned int>(remaining));
#else
        written = ::write(STDOUT_FILENO, ptr, remaining);
#endif
        if (written < 0) {
            if (errno == EINTR) continue;
            return 2;
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }

    return 0;
}

ProcessPoolResult ProcessPool::run(const std::vector<std::filesystem::path>& input_paths) {
#if defined(_WIN32)
    throw std::runtime_error("Multiprocess fork/exec mode is designed for UNIX/Linux platforms. Use --mode=threads on Windows.");
#else
    auto start_time = std::chrono::steady_clock::now();

    // 1. Traverse and discover all files
    std::vector<std::filesystem::path> all_files;
    uint64_t total_bytes = 0;

    for (const auto& root : input_paths) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(root, ec)) {
            all_files.push_back(root);
            total_bytes += std::filesystem::file_size(root, ec);
        } else if (std::filesystem::is_directory(root, ec)) {
            std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
            std::filesystem::recursive_directory_iterator end;
            while (it != end && !ec) {
                if (it->is_regular_file(ec)) {
                    all_files.push_back(it->path());
                    total_bytes += it->file_size(ec);
                }
                it.increment(ec);
            }
        }
    }

    if (all_files.empty()) {
        return {WordIndex{}, 0, 0, std::chrono::nanoseconds{0}, 0.0};
    }

    size_t num_shards = std::min(config_.num_procs, all_files.size());
    if (num_shards == 0) num_shards = 1;

    // 2. Partition files into shards
    std::vector<std::vector<std::filesystem::path>> shards(num_shards);
    for (size_t i = 0; i < all_files.size(); ++i) {
        shards[i % num_shards].push_back(all_files[i]);
    }

    // Ignore SIGPIPE so parent won't be abruptly killed on child pipe errors
    struct sigaction sa{}, old_sa{};
    sa.sa_handler = SIG_IGN;
    ::sigemptyset(&sa.sa_mask);
    ::sigaction(SIGPIPE, &sa, &old_sa);

    std::vector<pid_t> child_pids;
    std::vector<int> read_fds;
    std::vector<std::filesystem::path> manifest_paths;

    std::string io_str = io::to_string(config_.io_strategy);

    // 3. Launch children
    for (size_t i = 0; i < num_shards; ++i) {
        // Create manifest file
        auto manifest_path = std::filesystem::temp_directory_path() / 
            ("logforge_shard_" + std::to_string(::getpid()) + "_" + std::to_string(i) + ".txt");
        manifest_paths.push_back(manifest_path);

        std::ofstream manifest_file(manifest_path);
        for (const auto& f : shards[i]) {
            manifest_file << f.string() << "\n";
        }
        manifest_file.close();

        // Create pipe
        int pipe_fds[2];
#if defined(HAVE_PIPE2) && defined(O_CLOEXEC)
        if (::pipe2(pipe_fds, O_CLOEXEC) != 0) {
#else
        if (::pipe(pipe_fds) != 0) {
#endif
            int err = errno;
            throw std::system_error(err, std::generic_category(), "Failed to create IPC pipe: " + std::string(std::strerror(err)));
        }

        pid_t pid = ::fork();
        if (pid < 0) {
            int err = errno;
            ::close(pipe_fds[0]);
            ::close(pipe_fds[1]);
            throw std::system_error(err, std::generic_category(), "fork() failed: " + std::string(std::strerror(err)));
        }

        if (pid == 0) {
            // Child process
            ::close(pipe_fds[0]); // Close unused read end

            // Duplicate write end to standard output
            if (::dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
                ::perror("dup2 failed");
                ::_exit(127);
            }
            ::close(pipe_fds[1]);

            // Execute indexer binary in worker mode
            std::string manifest_str = manifest_path.string();
            std::string bin_str = config_.binary_path.string();

            char* const argv[] = {
                const_cast<char*>(bin_str.c_str()),
                const_cast<char*>("--worker-ipc"),
                const_cast<char*>("--manifest"),
                const_cast<char*>(manifest_str.c_str()),
                const_cast<char*>("--io"),
                const_cast<char*>(io_str.c_str()),
                nullptr
            };

            ::execv(config_.binary_path.c_str(), argv);

            // If execv returns, an error occurred
            std::cerr << "execv failed on " << config_.binary_path << ": " << std::strerror(errno) << "\n";
            ::_exit(127);
        }

        // Parent process
        ::close(pipe_fds[1]); // Close unused write end in parent
        child_pids.push_back(pid);
        read_fds.push_back(pipe_fds[0]);
    }

    // 4. Read partial indexes from child pipes
    WordIndex global_index;

    for (size_t i = 0; i < num_shards; ++i) {
        int rfd = read_fds[i];
        std::vector<uint8_t> buffer;
        std::vector<uint8_t> chunk(64 * 1024);

        while (true) {
            ssize_t bytes_read = ::read(rfd, chunk.data(), chunk.size());
            if (bytes_read < 0) {
                if (errno == EINTR) continue;
                std::cerr << "[IPC Error] Failed to read from child pipe: " << std::strerror(errno) << "\n";
                break;
            }
            if (bytes_read == 0) {
                break; // EOF
            }
            buffer.insert(buffer.end(), chunk.data(), chunk.data() + bytes_read);
        }
        ::close(rfd);

        if (!buffer.empty()) {
            try {
                WordIndex partial_index = persistence::Serializer::deserialize_from_buffer(buffer.data(), buffer.size());
                global_index.merge(std::move(partial_index));
            } catch (const std::exception& e) {
                std::cerr << "[IPC Error] Failed to deserialize child index shard " << i << ": " << e.what() << "\n";
            }
        }
    }

    // 5. Reap children with waitpid and inspect exit status
    for (size_t i = 0; i < child_pids.size(); ++i) {
        int status = 0;
        pid_t waited = ::waitpid(child_pids[i], &status, 0);
        if (waited > 0) {
            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0) {
                    std::cerr << "[IPC Warning] Child " << child_pids[i] << " exited with status " << exit_code << "\n";
                }
            } else if (WIFSIGNALED(status)) {
                std::cerr << "[IPC Warning] Child " << child_pids[i] << " killed by signal " << WTERMSIG(status) << "\n";
            }
        }
    }

    // 6. Clean up temporary manifest files
    for (const auto& m : manifest_paths) {
        std::error_code ec;
        std::filesystem::remove(m, ec);
    }

    // Restore original SIGPIPE handler
    ::sigaction(SIGPIPE, &old_sa, nullptr);

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    double seconds = std::chrono::duration<double>(elapsed).count();
    double total_mib = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    double mib_per_sec = seconds > 0.0 ? (total_mib / seconds) : 0.0;

    return {std::move(global_index), all_files.size(), total_bytes, elapsed, mib_per_sec};
#endif
}

} // namespace indexer::ipc
