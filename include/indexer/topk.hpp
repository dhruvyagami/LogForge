#pragma once

#include <indexer/index.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace indexer {

struct WordFreq {
    std::string word;
    uint64_t frequency{0};

    bool operator==(const WordFreq& other) const noexcept {
        return frequency == other.frequency && word == other.word;
    }
};

/**
 * @brief Comparator enforcing deterministic ranking:
 * 1. Frequency descending (higher frequency = higher rank)
 * 2. Word ascending (lexicographically smaller = higher rank)
 */
inline bool is_higher_rank(const WordFreq& a, const WordFreq& b) noexcept {
    if (a.frequency != b.frequency) {
        return a.frequency > b.frequency;
    }
    return a.word < b.word;
}

/**
 * @brief Extract top K most frequent words from an index using an O(N log K) min-heap.
 * 
 * Avoids full O(N log N) sorting. Resolves ties deterministically:
 * frequency desc, then word asc.
 * 
 * @param index The populated WordIndex.
 * @param k Number of top words to return.
 * @return std::vector<WordFreq> Sorted top-K list in descending rank order.
 */
std::vector<WordFreq> get_top_k(const WordIndex& index, size_t k);

} // namespace indexer
