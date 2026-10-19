#pragma once

#include <indexer/index.hpp>
#include <filesystem>

namespace indexer::persistence {

/**
 * @brief Crash-safe, atomic durable file persistence for Linux/UNIX.
 * 
 * Guarantees ACID durability via the POSIX protocol:
 * 1. Write payload to a unique temp file on the same filesystem.
 * 2. Invoke fsync(temp_fd) to flush contents and file metadata to physical storage.
 * 3. Invoke rename() to atomically swap temp file over target file.
 * 4. Open and fsync(parent_dir_fd) to flush the directory entry to persistent media.
 */
class DurableWriter {
public:
    /**
     * @brief Atomically and durably write a WordIndex to the specified target path.
     * 
     * @param target_path Target destination path.
     * @param index The WordIndex to serialize and persist.
     */
    static void write_atomic(const std::filesystem::path& target_path, const WordIndex& index);

    /**
     * @brief Load and verify the integrity of an index from the specified path.
     * 
     * @param target_path File path of the stored index.
     * @return WordIndex The verified and restored index.
     * @throws CorruptIndexException or std::system_error on failure.
     */
    static WordIndex read_verified(const std::filesystem::path& target_path);
};

} // namespace indexer::persistence
