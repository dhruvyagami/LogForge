#pragma once

#include <indexer/index.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <iosfwd>

namespace indexer {

struct WordDiff {
    std::string word;
    uint64_t old_freq{0};
    uint64_t new_freq{0};

    int64_t delta() const noexcept {
        return static_cast<int64_t>(new_freq) - static_cast<int64_t>(old_freq);
    }
};

struct IndexDiff {
    std::vector<WordDiff> added;    // In new_index, not in old_index
    std::vector<WordDiff> removed;  // In old_index, not in new_index
    std::vector<WordDiff> modified; // In both, but with changed frequency

    bool is_identical() const noexcept {
        return added.empty() && removed.empty() && modified.empty();
    }

    size_t total_differences() const noexcept {
        return added.size() + removed.size() + modified.size();
    }

    void print(std::ostream& os) const;
};

/**
 * @brief Compare two index snapshots and generate structured delta report.
 * 
 * Results in added, removed, and modified lists are sorted lexicographically (word asc).
 * 
 * @param old_index Baseline snapshot index.
 * @param new_index Subsequent snapshot index.
 * @return IndexDiff Detailed comparison structure.
 */
IndexDiff compute_diff(const WordIndex& old_index, const WordIndex& new_index);

} // namespace indexer
