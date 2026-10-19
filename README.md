# LogForge: High-Performance Concurrent UNIX/Linux File Indexer

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20POSIX-orange.svg)](https://www.kernel.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A production-grade, modular command-line text indexer for UNIX/Linux systems designed for high throughput, memory safety, concurrency, and crash resilience. LogForge recursively crawls files and directories, tokenizes text, builds word-frequency indexes, and supports Top-K queries, word frequency lookups, and snapshot version diffing.

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

## Key Features & Design Decisions

### 1. Zero-Allocation Streaming Tokenizer & Chunk Carryover
- Text is tokenized into lowercased alphanumeric tokens (`[a-zA-Z0-9]+`).
- When streaming through chunks (via `read()` or segmented inputs), words cut off at chunk boundaries are held in a `carryover_` buffer. The remainder in the succeeding chunk is spliced seamlessly with zero word distortion.
- Handled edge cases: arbitrary chunk sizes (down to 1 byte), files ending mid-word, delimiter-only inputs, and empty files.

### 2. Swappable I/O Layer (`--io=read|mmap`)
- **`ReadReader`**: Standard POSIX `open()` + `read()` into a reusable 64 KiB buffer. Fully handles `EINTR` retry loops, partial reads, and clean EOF signaling.
- **`MmapReader`**: Maps files directly into virtual memory via `mmap()` with `madvise(MADV_SEQUENTIAL)`. Zero user-kernel buffer copies on warm cache. Deterministic `munmap()` RAII cleanup in destructor. Gracefully handles 0-byte files without invoking `mmap(size=0)` (which POSIX rejects with `EINVAL`).

### 3. High-Concurrency Producer-Consumer Pipeline
- **Bounded Blocking Queue**: Implemented using `std::mutex` and two `std::condition_variable` primitives (`not_full`, `not_empty`). Bounded memory overhead prevents unbounded memory consumption on large directory trees.
- **Lock-Free Hot Path**: Worker threads maintain local `WordIndex` hash maps. No mutex contention on token insertions.
- **Graceful Shutdown**: `queue.close()` broadcasts to all waiting threads; remaining items are safely drained with zero deadlocks and zero busy-waiting.
- Final reduction merges all worker-local maps sequentially into the global index.

### 4. Crash-Safe Durable Persistence (Linux ACID semantics)
- Binary format containing:
  1. 8-byte magic header (`"LOGFORGE"`).
  2. Format version (`uint32_t`).
  3. Total token and entry counts.
  4. IEEE 802.3 CRC32 checksum over payload.
- **Atomic Persistence Protocol**:
  1. Serialized data is written to a unique temp file on the *same filesystem mount*.
  2. `fsync(temp_fd)` forces data and inode metadata to physical storage.
  3. `rename(temp, target)` atomically replaces target file.
  4. Parent directory is opened and synced via `fsync(parent_dir_fd)` to commit directory entry updates across system power failures.
- Verification: CRC32 checksum and format integrity are verified on deserialization. Tampered or truncated snapshots fail cleanly with `CorruptIndexException`.

### 5. Multiprocess IPC Mode (`--mode=procs`)
- Splits file inventory across $P$ worker processes.
- Forks children that `execv()` the indexer binary in `--worker-ipc` mode.
- Anonymous UNIX pipes stream serialized shard indexes back to parent.
- Handles `SIGPIPE` safely (`SIG_IGN`), validates child exit codes and termination signals via `waitpid()`.

### 6. Min-Heap Top-K Query
- Employs an $O(N \log K)$ bounded min-heap (`std::priority_queue`) instead of sorting the entire map ($O(N \log N)$).
- Enforces deterministic tie-breaking: `frequency desc`, then `word asc`.

### 7. Version Difference Query
- Compares two saved snapshot indexes and reports:
  - Words added.
  - Words removed.
  - Words with frequency changes (including exact deltas).

---

## Building the Project

### Prerequisites
- Modern C++17 compiler (`g++ >= 8`, `clang++ >= 9`)
- CMake >= 3.16
- Linux / UNIX environment (or WSL)

### Standard Release Build
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
```

### AddressSanitizer & UndefinedBehaviorSanitizer Build
```bash
mkdir build_asan && cd build_asan
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON ..
cmake --build . -j$(nproc)
```

### ThreadSanitizer Build
```bash
mkdir build_tsan && cd build_tsan
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON ..
cmake --build . -j$(nproc)
```

---

## Running Tests

Execute the comprehensive test suite verifying all modules:
```bash
cd build
./indexer_tests
# Or via CTest:
ctest --output-on-failure
```

---

## Usage Examples

### 1. Indexing Files and Directories
```bash
# Multithreaded indexing with mmap (default)
./indexer index --threads=8 --io=mmap --output=snapshot_v1.idx /var/log/syslog /var/log/nginx

# Multiprocess indexing with read()
./indexer index --mode=procs --threads=4 --io=read --output=snapshot_procs.idx /var/log/
```

### 2. Querying Word Frequency
```bash
./indexer query snapshot_v1.idx error
# Output: Word: 'error' => Frequency: 1420
```

### 3. Extracting Top-K Frequent Words
```bash
./indexer topk snapshot_v1.idx 10
```
Output:
```
========================================
           TOP-10 FREQUENT WORDS       
========================================
Rank    Word                   Frequency
----------------------------------------
1       the                        42190
2       error                      14200
3       warning                     8920
4       connection                  7410
5       failed                      6500
...
========================================
```

### 4. Version Difference Between Snapshots
```bash
./indexer diff snapshot_v1.idx snapshot_v2.idx
```

---

## Benchmarking Harness

Run the automated benchmark runner:
```bash
./scripts/run_benchmarks.sh 256 64
```

Or run the compiled benchmark binary directly:
```bash
# Syntax: ./indexer_bench [CorpusSizeMiB] [FileCount]
./build/indexer_bench 1024 128
```

### Understanding Warm vs Cold Cache
- **Warm Cache**: OS page cache holds the files in memory. `mmap` shines here by avoiding system call context switching and buffer copying, reaching thousands of MiB/s.
- **Cold Cache**: Reading directly from disk. `read()` with prefetching/readahead performs competitively with `mmap()`, which incurs minor page faults when accessing unmapped pages.
- To test cold cache on Linux:
  ```bash
  sudo sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'
  ./build/indexer_bench 1024 128
  ```

---

## Known Limitations & Future Directions
1. **Windows Subsystem Compatibility**: While the core logic, tokenizer, index, Top-K, diff, and threading are cross-platform, the `fork/exec` IPC and POSIX directory fsync specifically target UNIX/Linux kernels. On Windows, use `--mode=threads`.
2. **Dynamic Vocabulary Pruning**: For corpora with billions of unique words, external merge-sort or memory-mapped flat hash tables (e.g. Robin Hood hashing) can be adopted to index beyond physical RAM.
