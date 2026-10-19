#include "test_framework.hpp"
#include <indexer/persistence/serializer.hpp>
#include <indexer/persistence/durable_writer.hpp>

using namespace indexer;
using namespace indexer::persistence;

TEST_CASE(PersistenceRoundTrip) {
    WordIndex original;
    original.add_word("linux", 42);
    original.add_word("systems", 100);
    original.add_word("concurrency", 7);

    auto buffer = Serializer::serialize_to_buffer(original);
    ASSERT_TRUE(buffer.size() > sizeof(IndexHeader));

    WordIndex restored = Serializer::deserialize_from_buffer(buffer.data(), buffer.size());
    ASSERT_EQ(restored.unique_words(), original.unique_words());
    ASSERT_EQ(restored.total_words(), original.total_words());
    ASSERT_EQ(restored.get_frequency("linux"), 42u);
    ASSERT_EQ(restored.get_frequency("systems"), 100u);
    ASSERT_EQ(restored.get_frequency("concurrency"), 7u);
}

TEST_CASE(PersistenceCorruptionDetection) {
    WordIndex original;
    original.add_word("resilience", 50);
    original.add_word("integrity", 99);

    auto buffer = Serializer::serialize_to_buffer(original);

    // Corrupt one byte in payload
    buffer[sizeof(IndexHeader) + 2] ^= 0xFF;

    ASSERT_THROWS(Serializer::deserialize_from_buffer(buffer.data(), buffer.size()), CorruptIndexException);
}

TEST_CASE(PersistenceTruncationDetection) {
    WordIndex original;
    original.add_word("truncated", 10);

    auto buffer = Serializer::serialize_to_buffer(original);
    // Truncate last 4 bytes
    buffer.resize(buffer.size() - 4);

    ASSERT_THROWS(Serializer::deserialize_from_buffer(buffer.data(), buffer.size()), CorruptIndexException);
}

TEST_CASE(PersistenceInvalidMagic) {
    WordIndex original;
    original.add_word("test", 1);

    auto buffer = Serializer::serialize_to_buffer(original);
    // Tamper with magic header
    buffer[0] = 'X';

    ASSERT_THROWS(Serializer::deserialize_from_buffer(buffer.data(), buffer.size()), CorruptIndexException);
}

TEST_CASE(DurableWriterAtomicRoundTrip) {
    std::filesystem::path temp_index_path = std::filesystem::temp_directory_path() / "logforge_durable_test.idx";

    WordIndex original;
    original.add_word("atomic", 123);
    original.add_word("durable", 456);

    DurableWriter::write_atomic(temp_index_path, original);

    ASSERT_TRUE(std::filesystem::exists(temp_index_path));

    WordIndex loaded = DurableWriter::read_verified(temp_index_path);
    ASSERT_EQ(loaded.unique_words(), 2u);
    ASSERT_EQ(loaded.get_frequency("atomic"), 123u);
    ASSERT_EQ(loaded.get_frequency("durable"), 456u);

    std::error_code ec;
    std::filesystem::remove(temp_index_path, ec);
}
