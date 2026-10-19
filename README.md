# LogForge: High-Performance Concurrent UNIX/Linux File Indexer

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20POSIX-orange.svg)](https://www.kernel.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A production-grade, modular command-line text indexer for UNIX/Linux systems designed for high throughput, memory safety, concurrency, and crash resilience. LogForge recursively crawls files and directories, tokenizes text, builds word-frequency indexes, and supports Top-K queries, word frequency lookups, and snapshot version diffing.

---

## Table of Contents
1. [Architecture Overview](#architecture-overview)
2. [Directory Structure](#directory-structure)
3. [Component & Method API Reference](#component--method-api-reference)
   - [Tokenizer (`indexer::Tokenizer`)](#1-tokenizertokenizertokenizer)
   - [Word Index (`indexer::WordIndex`)](#2-word-indexindexerwordindex)
   - [Top-K Query (`indexer::TopK`)](#3-top-k-queryindexertopk)
   - [Version Diff (`indexer::Diff`)](#4-version-diffindexerdiff)
   - [RAII File Descriptor (`indexer::io::FileDescriptor`)](#5-raii-file-descriptorindexeriofiledescriptor)
   - [I/O Strategy Layer (`indexer::io::FileReader`)](#6-io-strategy-layerindexeriofilereader)
   - [Bounded Blocking Queue (`indexer::concurrency::BoundedQueue`)](#7-bounded-blocking-queueindexerconcurrencyboundedqueue)
   - [Multithreaded Pipeline (`indexer::concurrency::Pipeline`)](#8-multithreaded-pipelineindexerconcurrencypipeline)
   - [Binary Serialization (`indexer::persistence::Serializer`)](#9-binary-serializationindexerpersistenceserializer)
   - [Crash-Safe Durable Writer (`indexer::persistence::DurableWriter`)](#10-crash-safe-durable-writerindexerpersistencedurablewriter)
   - [Fork/Exec Process Pool (`indexer::ipc::ProcessPool`)](#11-forkexec-process-poolindexeripcprocesspool)
4. [CLI Command Reference](#cli-command-reference)
5. [C++ Programmatic Usage Examples](#c-programmatic-usage-examples)
6. [Building and Running](#building-and-running)
7. [Benchmarking & Cache Mechanics](#benchmarking--cache-mechanics)

---

## Architecture Overview

```
                               +-----------------------------+
                               |     Filesystem Traversal    |
                               |      (Producer Thread)      |
                               +--------------+--------------+
                                              |
                                              | pushes paths
                                              v
                              +-------------------------------+
                              |    Bounded Blocking Queue     |
                              |  (Capacity bounded, condvars) |
                              +-------+---------------+-------+
                                      |               |
                         pops path    |               | pops path
                                      v               v
                   +--------------------+           +--------------------+
                   |  Worker Thread 1   |           |  Worker Thread N   |
                   |  (Local Index)     |           |  (Local Index)     |
                   |  - FileReader (IO) |           |  - FileReader (IO) |
                   |  - Tokenizer       |           |  - Tokenizer       |
                   +----------+---------+           +---------+----------+
                              |                               |
                              +---------------+---------------+
                                              |
                                              | lock-free local reduction
                                              v
                              +-------------------------------+
                              |      Global Merged Index      |
                              +---------------+---------------+
                                              |
                                              | Atomic Crash-Safe Write
                                              v
                              +-------------------------------+
                              | 1. write(temp_file)           |
                              | 2. fsync(temp_file)           |
                              | 3. rename(temp, target)       |
                              | 4. fsync(parent_directory)    |
                              +-------------------------------+
```

---

## Directory Structure

```
LogForge/
├── CMakeLists.txt
├── README.md
├── include/
│   └── indexer/
│       ├── tokenizer.hpp                 # Streaming tokenizer with chunk carryover
│       ├── index.hpp                     # WordIndex frequency map and merge engine
│       ├── topk.hpp                      # Min-heap Top-K query (O(N log K))
│       ├── diff.hpp                      # Snapshot version-diff comparator
│       ├── io/
│       │   ├── file_descriptor.hpp       # RAII POSIX file descriptor wrapper
│       │   ├── file_reader.hpp           # Abstract I/O strategy interface & factory
│       │   ├── read_reader.hpp           # POSIX read() buffer implementation
│       │   └── mmap_reader.hpp           # POSIX mmap() + madvise implementation
│       ├── concurrency/
│       │   ├── bounded_queue.hpp         # Bounded blocking queue with 2 condvars
│       │   └── pipeline.hpp              # Multithreaded producer-consumer pipeline
│       ├── persistence/
│       │   ├── serializer.hpp            # Binary serializer with IEEE 802.3 CRC32
│       │   └── durable_writer.hpp        # Atomic rename + fsync durability engine
│       └── ipc/
│           └── process_pool.hpp          # Fork/exec IPC mode with pipes & waitpid
├── src/
│   ├── tokenizer.cpp
│   ├── index.cpp
│   ├── topk.cpp
│   ├── diff.cpp
│   ├── main.cpp                          # CLI driver & query commands
│   ├── io/
│   │   ├── read_reader.cpp
│   │   └── mmap_reader.cpp
│   ├── concurrency/
│   │   └── pipeline.cpp
│   ├── persistence/
│   │   ├── serializer.cpp
│   │   └── durable_writer.cpp
│   └── ipc/
│       └── process_pool.cpp
├── tests/
│   ├── test_framework.hpp                # Zero-dependency test harness
│   ├── test_main.cpp                     # Test runner
│   ├── test_tokenizer.cpp
│   ├── test_index.cpp
│   ├── test_topk.cpp
│   ├── test_diff.cpp
│   ├── test_bounded_queue.cpp
│   ├── test_io.cpp
│   ├── test_persistence.cpp
│   └── test_pipeline.cpp
├── bench/
│   └── benchmark.cpp                     # Benchmarking harness (real measurements)
└── scripts/
    ├── generate_corpus.py                # Synthetic Zipfian corpus generator
    └── run_benchmarks.sh                 # Full automated build & benchmark runner
```

---

## Component & Method API Reference

### 1. Tokenizer (`indexer::Tokenizer`)
*Header:* `include/indexer/tokenizer.hpp`  
*Source:* `src/tokenizer.cpp`

The streaming tokenizer splits arbitrary text into lowercase alphanumeric tokens (`[a-zA-Z0-9]+`), treating all non-alphanumeric bytes as delimiters. Crucially, it manages state across I/O chunk boundaries.

#### Methods:
- `void process_chunk(const char* data, size_t size, bool is_eof, const std::function<void(std::string_view)>& on_token)`
  - **Purpose**: Parses an incoming memory buffer chunk.
  - **Parameters**:
    - `data`: Pointer to raw chunk memory (or `nullptr` if size is 0).
    - `size`: Length of chunk in bytes.
    - `is_eof`: Flags whether this is the final chunk in the stream.
    - `on_token`: Callback invoked with each extracted token.
  - **Boundary Guarantee**: If a word starts in chunk $N$ and continues into chunk $N+1$, the partial token is stored in `carryover_` and emitted as a single unified word once completed.
- `void reset() noexcept`
  - **Purpose**: Clears the internal `carryover_` buffer to prepare the tokenizer instance for a new file.
- `bool has_carryover() const noexcept`
  - **Purpose**: Returns `true` if a partial word is currently awaiting chunk completion.
- `static std::vector<std::string> tokenize_string(std::string_view text)`
  - **Purpose**: Synchronously tokenizes a complete string in-memory.
- `static inline char to_lower_ascii(char c) noexcept`
  - **Purpose**: Fast branchless/constant ASCII conversion for upper-to-lower characters (`'A'`-`'Z'`).
- `static inline bool is_alnum_ascii(char c) noexcept`
  - **Purpose**: Returns `true` if `c` is within `[a-zA-Z0-9]`.

---

### 2. Word Index (`indexer::WordIndex`)
*Header:* `include/indexer/index.hpp`  
*Source:* `src/index.cpp`

Encapsulates aggregate word counts using an underlying hash map (`std::unordered_map<std::string, uint64_t>`). Designed for lock-free thread-local accumulation.

#### Methods:
- `void add_word(std::string_view word, uint64_t count = 1)`
  - **Purpose**: Increments the frequency count for `word` by `count`.
- `void add_word(const std::string& word, uint64_t count = 1)`
  - **Purpose**: Overload avoiding extra allocation when passing an existing string key.
- `uint64_t get_frequency(std::string_view word) const`
  - **Purpose**: Retrieves frequency for `word`. Returns `0` if word is not indexed.
- `bool contains(std::string_view word) const`
  - **Purpose**: Returns `true` if `word` exists with frequency $\ge 1$.
- `size_t unique_words() const noexcept`
  - **Purpose**: Returns the number of distinct unique words in the index.
- `uint64_t total_words() const noexcept`
  - **Purpose**: Returns the sum of all word occurrences combined.
- `bool empty() const noexcept`
  - **Purpose**: Returns `true` if the index has no entries.
- `void merge(const WordIndex& other)`
  - **Purpose**: Merges frequencies from `other` into `*this`.
- `void merge(WordIndex&& other)`
  - **Purpose**: Move-optimized merge. If `*this` is empty, steals internal storage directly without iteration.
- `void clear() noexcept`
  - **Purpose**: Empties all records and resets counters to zero.
- `const MapType& data() const noexcept`
  - **Purpose**: Exposes direct read-only access to the underlying hash map.
- `const_iterator begin() const noexcept` / `const_iterator end() const noexcept`
  - **Purpose**: Standard const iterators over `(word, count)` key-value pairs.

---

### 3. Top-K Query (`indexer::TopK`)
*Header:* `include/indexer/topk.hpp`  
*Source:* `src/topk.cpp`

Extracts the top $K$ most frequent words without performing a full $O(N \log N)$ sort of the entire index.

#### Structures & Methods:
- `struct WordFreq`:
  - `std::string word`: The word token.
  - `uint64_t frequency`: Total count.
- `bool is_higher_rank(const WordFreq& a, const WordFreq& b) noexcept`
  - **Deterministic Ranking Rules**:
    1. Frequency descending (`a.frequency > b.frequency`).
    2. Word ascending (`a.word < b.word`) as a tie-breaker.
- `std::vector<WordFreq> get_top_k(const WordIndex& index, size_t k)`
  - **Algorithm**:
    - If $k \ge index.unique\_words()$, collects all entries and sorts them in $O(N \log N)$.
    - Otherwise, maintains a **bounded min-heap** (`std::priority_queue`) of size $k$ in **$O(N \log K)$** time and **$O(K)$** auxiliary space.
  - **Return**: A vector of size $\min(k, N)$ sorted in descending rank order.

---

### 4. Version Diff (`indexer::Diff`)
*Header:* `include/indexer/diff.hpp`  
*Source:* `src/diff.cpp`

Compares two index snapshots (e.g. before and after a software release or log run) to compute differential delta metrics.

#### Structures & Methods:
- `struct WordDiff`:
  - `std::string word`: The target token.
  - `uint64_t old_freq`: Count in baseline snapshot (0 if newly added).
  - `uint64_t new_freq`: Count in subsequent snapshot (0 if removed).
  - `int64_t delta() const noexcept`: Returns `new_freq - old_freq`.
- `struct IndexDiff`:
  - `std::vector<WordDiff> added`: Tokens present only in the new snapshot.
  - `std::vector<WordDiff> removed`: Tokens present only in the old snapshot.
  - `std::vector<WordDiff> modified`: Tokens whose frequencies changed.
  - `bool is_identical() const noexcept`: Returns `true` if no additions, removals, or modifications exist.
  - `size_t total_differences() const noexcept`: Total count of differences.
  - `void print(std::ostream& os) const`: Formats an ASCII report with aligned columns and deltas.
- `IndexDiff compute_diff(const WordIndex& old_index, const WordIndex& new_index)`
  - **Purpose**: Computes differential analysis and returns lexicographically sorted result lists.

---

### 5. RAII File Descriptor (`indexer::io::FileDescriptor`)
*Header:* `include/indexer/io/file_descriptor.hpp`

Move-only RAII wrapper around integer POSIX file descriptors to eliminate descriptor leaks across error paths and exceptions.

#### Methods:
- `FileDescriptor(int fd) noexcept`: Adopts ownership of raw descriptor `fd`.
- `~FileDescriptor()`: Invokes `::close(fd_)` automatically if `fd_ >= 0`.
- `int get() const noexcept`: Accesses raw integer descriptor.
- `bool is_valid() const noexcept`: Returns `true` if `fd_ >= 0`.
- `int release() noexcept`: Releases ownership and returns integer without closing.
- `void reset(int new_fd = -1) noexcept`: Closes current descriptor and assigns `new_fd`.

---

### 6. I/O Strategy Layer (`indexer::io::FileReader`)
*Header:* `include/indexer/io/file_reader.hpp`, `read_reader.hpp`, `mmap_reader.hpp`  
*Source:* `src/io/read_reader.cpp`, `src/io/mmap_reader.cpp`

Swappable storage reading strategies supporting POSIX standard buffer streaming or kernel memory mapping.

#### `FileReader` (Abstract Interface):
- `virtual const std::filesystem::path& path() const noexcept = 0`
- `virtual uint64_t file_size() const noexcept = 0`
- `virtual IOStrategy strategy() const noexcept = 0` (`IOStrategy::Read` or `IOStrategy::Mmap`)
- `virtual void read_all(const std::function<void(const char* data, size_t size, bool is_eof)>& on_chunk) = 0`
- Factory helper: `std::unique_ptr<FileReader> create_file_reader(const std::filesystem::path& path, IOStrategy strategy, size_t buffer_size = 64 * 1024)`

#### `ReadReader`:
- Uses POSIX `open()` with `O_RDONLY | O_CLOEXEC`.
- Reads chunks into reusable buffer (default: 64 KiB).
- Handles `EINTR` signal retry loops and short reads.
- Safely handles 0-byte empty files by signaling an immediate EOF chunk (`nullptr, 0, true`).

#### `MmapReader`:
- Uses POSIX `open()` and `fstat()` to determine file size.
- Gracefully handles empty files (avoids calling `mmap` with length 0 which fails with `EINVAL`).
- Maps pages with `mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0)`.
- Informs kernel readahead with `madvise(addr, size, MADV_SEQUENTIAL)`.
- Deterministically cleans up memory in destructor via `munmap(mapped_data_, file_size_)`.

---

### 7. Bounded Blocking Queue (`indexer::concurrency::BoundedQueue`)
*Header:* `include/indexer/concurrency/bounded_queue.hpp`

Thread-safe bounded FIFO queue coordinating producer and consumer worker threads with bounded memory usage.

#### Methods:
- `explicit BoundedQueue(size_t capacity)`
  - **Purpose**: Initializes queue with fixed maximum item capacity.
- `bool push(T item)`
  - **Purpose**: Enqueues an item. Blocks on `not_full_` condition variable if `queue_.size() >= capacity_`. Returns `false` if queue was closed.
- `std::optional<T> pop()`
  - **Purpose**: Dequeues an item. Blocks on `not_empty_` condition variable if queue is empty. Returns `std::nullopt` once the queue is closed and fully drained.
- `void close()`
  - **Purpose**: Closes queue to future pushes and broadcasts to both `not_empty_` and `not_full_` condition variables to wake blocked threads.
- `bool is_closed() const` / `size_t size() const` / `bool empty() const` / `size_t capacity() const`
  - **Purpose**: Queue inspection primitives.

---

### 8. Multithreaded Pipeline (`indexer::concurrency::Pipeline`)
*Header:* `include/indexer/concurrency/pipeline.hpp`  
*Source:* `src/concurrency/pipeline.cpp`

Coordinates multithreaded directory crawling and chunked indexing.

#### Structures & Methods:
- `struct PipelineConfig`:
  - `size_t num_threads`: Number of worker threads (`0` = auto-detect hardware CPU cores).
  - `IOStrategy io_strategy`: `Read` or `Mmap`.
  - `size_t queue_capacity`: Maximum queue depth (default: 1024).
- `struct PipelineStats`:
  - `size_t files_processed`: Total count of files indexed.
  - `uint64_t total_bytes`: Total byte volume indexed.
  - `std::chrono::nanoseconds duration`: Elapsed wall-clock time.
  - `double throughput_mib_s`: Measured processing throughput in MiB/s.
- `struct PipelineResult`:
  - `WordIndex index`: Final merged global index.
  - `PipelineStats stats`: Metrics snapshot.
- `PipelineResult run(const std::vector<std::filesystem::path>& input_paths)`
  - **Lifecycle**:
    1. Spawns 1 producer thread that crawls paths using `recursive_directory_iterator` and pushes file paths.
    2. Spawns $N$ worker threads; each worker pops files, reads chunks, and inserts tokens into its **thread-local** `WordIndex` (zero mutex locking on hot path).
    3. Producer completes and closes queue; workers drain queue and terminate.
    4. Main thread executes a reduction step, merging all worker-local indexes into the final `WordIndex`.

---

### 9. Binary Serialization (`indexer::persistence::Serializer`)
*Header:* `include/indexer/persistence/serializer.hpp`  
*Source:* `src/persistence/serializer.cpp`

High-speed binary serializer with format versioning and full payload CRC32 validation.

#### Binary Layout:
```
+--------------------------------------------------------------+
| Magic: "LOGFORGE" (8B) | Version (4B) | Flags (4B)           |
+--------------------------------------------------------------+
| Entry Count (8B)       | Total Words (8B) | CRC32 (4B)       |
+--------------------------------------------------------------+
| Payload:                                                     |
|   [word_len: 4B][word_bytes: N][count: 8B] ...             |
+--------------------------------------------------------------+
```

#### Methods:
- `static void serialize(const WordIndex& index, std::ostream& out)`
  - **Purpose**: Writes serialized binary stream to `out`.
- `static std::vector<uint8_t> serialize_to_buffer(const WordIndex& index)`
  - **Purpose**: Serializes index into a contiguous in-memory byte buffer.
- `static WordIndex deserialize(std::istream& in)`
  - **Purpose**: Reads stream, verifies magic and CRC32, and restores index.
- `static WordIndex deserialize_from_buffer(const uint8_t* data, size_t size)`
  - **Purpose**: Deserializes from raw memory pointer. Throws `CorruptIndexException` if magic, version, length, or CRC32 fails.
- `static uint32_t compute_crc32(const uint8_t* data, size_t length, uint32_t initial = 0)`
  - **Purpose**: Computes IEEE 802.3 CRC32 checksum over byte slice using precomputed 256-entry table.

---

### 10. Crash-Safe Durable Writer (`indexer::persistence::DurableWriter`)
*Header:* `include/indexer/persistence/durable_writer.hpp`  
*Source:* `src/persistence/durable_writer.cpp`

Guarantees atomic, crash-proof disk persistence conforming to Linux ACID filesystem standards.

#### Methods:
- `static void write_atomic(const std::filesystem::path& target_path, const WordIndex& index)`
  - **The 4-Step Durability Protocol**:
    1. Writes serialized payload into a temporary file in the *same directory/filesystem mount* (`<target>.tmp.<pid>.<id>`).
    2. Invokes `::fsync(temp_fd)` to guarantee file bytes and inode metadata are physically written to storage media.
    3. Invokes POSIX `::rename(temp_path, target_path)` to perform an atomic pointer swap on the filesystem.
    4. Opens the parent directory (`O_DIRECTORY`) and calls `::fsync(dir_fd)` to ensure directory table entry persistence across power failures.
- `static WordIndex read_verified(const std::filesystem::path& target_path)`
  - **Purpose**: Reads index from disk, checks file integrity, and returns verified `WordIndex`.

---

### 11. Fork/Exec Process Pool (`indexer::ipc::ProcessPool`)
*Header:* `include/indexer/ipc/process_pool.hpp`  
*Source:* `src/ipc/process_pool.cpp`

Multi-process parallelism alternative to threads (`--mode=procs`), isolating workers in separate Linux address spaces.

#### Methods:
- `explicit ProcessPool(ProcessPoolConfig config)`
  - **Purpose**: Configures number of processes and locates self executable path.
- `ProcessPoolResult run(const std::vector<std::filesystem::path>& input_paths)`
  - **Lifecycle**:
    1. Collects all target files and partitions them into $P$ shards.
    2. Writes temporary shard manifests.
    3. Creates unidirectional UNIX pipes (`::pipe()`).
    4. Forks child processes (`::fork()`); children redirect pipe write-end to `STDOUT_FILENO` and `::execv()` self with `--worker-ipc`.
    5. Parent ignores `SIGPIPE`, reads serialized partial indexes from each child pipe, merges them, and cleans up children using `::waitpid()`.
- `static int run_worker_mode(const std::filesystem::path& manifest_path, io::IOStrategy strategy)`
  - **Purpose**: Child execution entry point. Reads shard files, indexes them, streams binary payload to standard output, and exits `0`.

---

## CLI Command Reference

The `indexer` CLI driver supports four subcommands and flexible runtime flags:

### 1. `index`
Indexes target files or directories.
```bash
./indexer index [options] <path...>
```
| Flag | Values | Default | Description |
| :--- | :--- | :--- | :--- |
| `--threads=N` | Integer $\ge 1$ | Hardware Cores | Number of concurrent worker threads or processes |
| `--io=STRAT` | `read`, `mmap` | `mmap` | I/O strategy (`read()` buffer vs `mmap()` virtual memory) |
| `--mode=MODE` | `threads`, `procs` | `threads` | Concurrency model (multithreading vs fork/exec IPC) |
| `--output=FILE`| File path | *(none)* | Atomically and durably persists resulting index to `FILE` |

### 2. `query`
Looks up the occurrence count of a single word in a persisted index.
```bash
./indexer query <index_file> <word>
```
- Example: `./indexer query snapshot.idx warning`
- Output: `Word: 'warning' => Frequency: 184`

### 3. `topk`
Extracts the Top $K$ most frequent words in deterministic order.
```bash
./indexer topk <index_file> <K>
```
- Example: `./indexer topk snapshot.idx 10`

### 4. `diff`
Computes differences between two index snapshots.
```bash
./indexer diff <old_index_file> <new_index_file>
```
- Reports added words, removed words, and frequency changes with exact integer deltas.

---

## C++ Programmatic Usage Examples

### Example 1: In-Memory Tokenizing & Indexing
```cpp
#include <indexer/tokenizer.hpp>
#include <indexer/index.hpp>
#include <iostream>

int main() {
    indexer::WordIndex index;
    indexer::Tokenizer tokenizer;

    std::string text = "Linux kernel concurrency: mmap and read syscalls.";
    tokenizer.process_chunk(text.data(), text.size(), true, [&index](std::string_view token) {
        index.add_word(token);
    });

    std::cout << "Unique words: " << index.unique_words() << "\n";
    std::cout << "Count of 'concurrency': " << index.get_frequency("concurrency") << "\n";
    return 0;
}
```

### Example 2: Multithreaded Pipeline Execution
```cpp
#include <indexer/concurrency/pipeline.hpp>
#include <indexer/persistence/durable_writer.hpp>

int main() {
    indexer::concurrency::PipelineConfig config;
    config.num_threads = 8;
    config.io_strategy = indexer::io::IOStrategy::Mmap;

    indexer::concurrency::Pipeline pipeline(config);
    auto result = pipeline.run({"/var/log", "/home/user/logs"});

    // Atomically persist with CRC32 checksum
    indexer::persistence::DurableWriter::write_atomic("logs_snapshot.idx", result.index);
    return 0;
}
```

### Example 3: Extracting Top-10 Words
```cpp
#include <indexer/persistence/durable_writer.hpp>
#include <indexer/topk.hpp>
#include <iostream>

int main() {
    indexer::WordIndex index = indexer::persistence::DurableWriter::read_verified("logs_snapshot.idx");
    auto top10 = indexer::get_top_k(index, 10);

    for (const auto& item : top10) {
        std::cout << item.word << ": " << item.frequency << "\n";
    }
    return 0;
}
```

---

## Building and Running

### Prerequisites
- C++17 compliant compiler (`g++ >= 8`, `clang++ >= 9`)
- CMake >= 3.16
- Linux or POSIX environment (e.g., Ubuntu, Debian, WSL)

### Compilation
```bash
# Release Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# AddressSanitizer & UndefinedBehaviorSanitizer Build
mkdir build_asan && cd build_asan
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON ..
cmake --build . -j$(nproc)

# ThreadSanitizer Build
mkdir build_tsan && cd build_tsan
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON ..
cmake --build . -j$(nproc)
```

### Running Test Suite
```bash
./build/indexer_tests
```

---

## Benchmarking & Cache Mechanics

Execute the benchmark suite across synthetic datasets:
```bash
# Syntax: ./indexer_bench [CorpusSizeMiB] [FileCount]
./build/indexer_bench 512 64
```

### Cache Mechanics
- **Warm Cache**: OS page cache holds files in physical RAM. `mmap` operates with zero-copy efficiency, bypassing kernel-to-user buffer copying and syscall overhead.
- **Cold Cache**: Reading directly from physical NVMe/SATA storage. `read()` initiates larger kernel readahead windows, whereas `mmap` incurs minor page faults when first accessing pages.
- **Cold Cache Testing Command**:
  ```bash
  sudo sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'
  ./build/indexer_bench 512 64
  ```
