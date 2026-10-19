#include <indexer/persistence/serializer.hpp>
#include <cstring>
#include <sstream>
#include <array>

namespace indexer::persistence {

namespace {

// Lookup table generator for IEEE 802.3 CRC-32 (polynomial 0xEDB88320)
constexpr auto make_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32_TABLE = make_crc32_table();

} // anonymous namespace

uint32_t Serializer::compute_crc32(const uint8_t* data, size_t length, uint32_t initial) {
    uint32_t crc = ~initial;
    for (size_t i = 0; i < length; ++i) {
        uint8_t table_idx = static_cast<uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ CRC32_TABLE[table_idx];
    }
    return ~crc;
}

std::vector<uint8_t> Serializer::serialize_to_buffer(const WordIndex& index) {
    // Stage 1: Build payload in memory to compute CRC32
    std::vector<uint8_t> payload;
    // Estimate 24 bytes per entry on average
    payload.reserve(index.unique_words() * 24);

    for (const auto& [word, count] : index) {
        uint32_t word_len = static_cast<uint32_t>(word.size());
        
        // Append word_len
        const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&word_len);
        payload.insert(payload.end(), len_bytes, len_bytes + sizeof(word_len));

        // Append word chars
        payload.insert(payload.end(), word.data(), word.data() + word.size());

        // Append count
        const uint8_t* count_bytes = reinterpret_cast<const uint8_t*>(&count);
        payload.insert(payload.end(), count_bytes, count_bytes + sizeof(count));
    }

    uint32_t checksum = compute_crc32(payload.data(), payload.size());

    // Stage 2: Create header
    IndexHeader header{};
    std::memcpy(header.magic, MAGIC, sizeof(MAGIC));
    header.version = CURRENT_VERSION;
    header.flags = 0;
    header.entry_count = index.unique_words();
    header.total_words = index.total_words();
    header.checksum = checksum;

    // Stage 3: Assemble complete buffer (Header + Payload)
    std::vector<uint8_t> result;
    result.reserve(sizeof(IndexHeader) + payload.size());

    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    result.insert(result.end(), header_bytes, header_bytes + sizeof(IndexHeader));
    result.insert(result.end(), payload.begin(), payload.end());

    return result;
}

void Serializer::serialize(const WordIndex& index, std::ostream& out) {
    std::vector<uint8_t> buffer = serialize_to_buffer(index);
    out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (!out) {
        throw std::runtime_error("Failed to write serialized index data to output stream");
    }
}

WordIndex Serializer::deserialize_from_buffer(const uint8_t* data, size_t size) {
    if (size < sizeof(IndexHeader)) {
        throw CorruptIndexException("Index data size is smaller than header size");
    }

    IndexHeader header{};
    std::memcpy(&header, data, sizeof(IndexHeader));

    if (std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0) {
        throw CorruptIndexException("Invalid magic identifier in header");
    }

    if (header.version != CURRENT_VERSION) {
        throw CorruptIndexException("Unsupported index format version: " + std::to_string(header.version));
    }

    const uint8_t* payload = data + sizeof(IndexHeader);
    size_t payload_size = size - sizeof(IndexHeader);

    // Verify CRC32 checksum before parsing payload
    uint32_t computed_checksum = compute_crc32(payload, payload_size);
    if (computed_checksum != header.checksum) {
        throw CorruptIndexException("CRC32 checksum mismatch: expected 0x" + 
            std::to_string(header.checksum) + ", computed 0x" + std::to_string(computed_checksum));
    }

    WordIndex index;
    size_t offset = 0;

    for (uint64_t i = 0; i < header.entry_count; ++i) {
        if (offset + sizeof(uint32_t) > payload_size) {
            throw CorruptIndexException("Unexpected end of payload reading word length at entry " + std::to_string(i));
        }

        uint32_t word_len = 0;
        std::memcpy(&word_len, payload + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + word_len + sizeof(uint64_t) > payload_size) {
            throw CorruptIndexException("Unexpected end of payload reading word payload at entry " + std::to_string(i));
        }

        std::string word(reinterpret_cast<const char*>(payload + offset), word_len);
        offset += word_len;

        uint64_t count = 0;
        std::memcpy(&count, payload + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);

        index.add_word(word, count);
    }

    if (offset != payload_size) {
        throw CorruptIndexException("Extraneous bytes detected after deserializing all entries");
    }

    return index;
}

WordIndex Serializer::deserialize(std::istream& in) {
    in.seekg(0, std::ios::end);
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (size <= 0) {
        throw CorruptIndexException("Cannot deserialize empty or unseekable stream");
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw CorruptIndexException("Failed to read index stream into memory");
    }

    return deserialize_from_buffer(buffer.data(), buffer.size());
}

} // namespace indexer::persistence
