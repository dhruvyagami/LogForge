#pragma once

#include <indexer/index.hpp>
#include <iostream>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace indexer::persistence {

class CorruptIndexException : public std::runtime_error {
public:
    explicit CorruptIndexException(const std::string& msg) 
        : std::runtime_error("CorruptIndexException: " + msg) {}
};

#pragma pack(push, 1)
struct IndexHeader {
    char magic[8];        // "LOGFORGE"
    uint32_t version;     // Format version (currently 1)
    uint32_t flags;       // Reserved flags
    uint64_t entry_count; // Number of unique word records
    uint64_t total_words; // Aggregate sum of all word frequencies
    uint32_t checksum;    // CRC32 checksum over the payload
};
#pragma pack(pop)

/**
 * @brief High-speed binary index serializer with CRC32 integrity verification.
 */
class Serializer {
public:
    static constexpr char MAGIC[8] = {'L', 'O', 'G', 'F', 'O', 'R', 'G', 'E'};
    static constexpr uint32_t CURRENT_VERSION = 1;

    /**
     * @brief Serialize WordIndex into binary stream with header and CRC32 checksum.
     */
    static void serialize(const WordIndex& index, std::ostream& out);

    /**
     * @brief Serialize WordIndex directly into a byte buffer in memory.
     */
    static std::vector<uint8_t> serialize_to_buffer(const WordIndex& index);

    /**
     * @brief Deserialize WordIndex from binary stream.
     * 
     * Verifies magic bytes, version compatibility, entry count, and CRC32 checksum.
     * @throws CorruptIndexException on any validation failure.
     */
    static WordIndex deserialize(std::istream& in);

    /**
     * @brief Deserialize WordIndex from memory buffer.
     * @throws CorruptIndexException on any validation failure.
     */
    static WordIndex deserialize_from_buffer(const uint8_t* data, size_t size);

    /**
     * @brief Compute standard IEEE 802.3 CRC32 checksum over a byte range.
     */
    static uint32_t compute_crc32(const uint8_t* data, size_t length, uint32_t initial = 0);
};

} // namespace indexer::persistence
