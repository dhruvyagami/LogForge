#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

namespace indexer {

/**
 * @brief High-efficiency word frequency index.
 * 
 * Stores mappings from canonical lowercased word tokens to their aggregate frequency.
 * Designed for lock-free thread-local construction followed by hierarchical or
 * sequential reduction/merge into a global index.
 */
class WordIndex {
public:
    using MapType = std::unordered_map<std::string, uint64_t>;
    using const_iterator = MapType::const_iterator;
    using iterator = MapType::iterator;

    WordIndex() = default;
    ~WordIndex() = default;

    WordIndex(const WordIndex&) = default;
    WordIndex& operator=(const WordIndex&) = default;
    WordIndex(WordIndex&&) noexcept = default;
    WordIndex& operator=(WordIndex&&) noexcept = default;

    /**
     * @brief Record occurrence(s) of a word.
     */
    void add_word(std::string_view word, uint64_t count = 1);
    void add_word(const std::string& word, uint64_t count = 1);

    /**
     * @brief Query count of a given word. Returns 0 if word is not indexed.
     */
    uint64_t get_frequency(std::string_view word) const;
    uint64_t get_frequency(const std::string& word) const;

    /**
     * @brief Check if index contains a given word.
     */
    bool contains(std::string_view word) const;

    /**
     * @brief Total number of distinct unique words.
     */
    size_t unique_words() const noexcept { return frequencies_.size(); }

    /**
     * @brief Total count of all words combined.
     */
    uint64_t total_words() const noexcept { return total_count_; }

    /**
     * @brief Check if index is empty.
     */
    bool empty() const noexcept { return frequencies_.empty(); }

    /**
     * @brief Merge another index into this index (combining frequencies).
     */
    void merge(const WordIndex& other);
    void merge(WordIndex&& other);

    /**
     * @brief Clear all entries and reset totals.
     */
    void clear() noexcept;

    /**
     * @brief Direct access to underlying map for iteration.
     */
    const MapType& data() const noexcept { return frequencies_; }

    const_iterator begin() const noexcept { return frequencies_.begin(); }
    const_iterator end() const noexcept { return frequencies_.end(); }

    bool operator==(const WordIndex& other) const noexcept;
    bool operator!=(const WordIndex& other) const noexcept { return !(*this == other); }

private:
    MapType frequencies_;
    uint64_t total_count_{0};
};

} // namespace indexer
