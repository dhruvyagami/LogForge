#include <indexer/topk.hpp>
#include <queue>
#include <algorithm>

namespace indexer {

namespace {

// Comparator for min-heap:
// std::priority_queue orders so that the element that compares "less" under Compare
// is placed below.
// If Comp(a, b) returns true when a is BETTER than b,
// then the WORST element is maintained at top().
struct MinHeapCompare {
    bool operator()(const WordFreq& a, const WordFreq& b) const noexcept {
        return is_higher_rank(a, b);
    }
};

} // anonymous namespace

std::vector<WordFreq> get_top_k(const WordIndex& index, size_t k) {
    if (k == 0 || index.empty()) {
        return {};
    }

    if (k >= index.unique_words()) {
        // Collect all and sort
        std::vector<WordFreq> result;
        result.reserve(index.unique_words());
        for (const auto& [word, freq] : index) {
            result.push_back({word, freq});
        }
        std::sort(result.begin(), result.end(), is_higher_rank);
        return result;
    }

    // Min-heap bounded to size k
    std::priority_queue<WordFreq, std::vector<WordFreq>, MinHeapCompare> min_heap;

    for (const auto& [word, freq] : index) {
        WordFreq current{word, freq};

        if (min_heap.size() < k) {
            min_heap.push(std::move(current));
        } else {
            // If current is higher rank than the worst element in heap, replace it
            if (is_higher_rank(current, min_heap.top())) {
                min_heap.pop();
                min_heap.push(std::move(current));
            }
        }
    }

    // Extract elements from min-heap (they emerge from worst to best)
    std::vector<WordFreq> result;
    result.reserve(min_heap.size());
    while (!min_heap.empty()) {
        result.push_back(std::move(const_cast<WordFreq&>(min_heap.top())));
        min_heap.pop();
    }

    // Reverse to achieve best-to-worst (rank descending) order
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace indexer
