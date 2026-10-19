#include <indexer/index.hpp>

namespace indexer {

void WordIndex::add_word(std::string_view word, uint64_t count) {
    if (word.empty() || count == 0) return;
    std::string key(word);
    frequencies_[key] += count;
    total_count_ += count;
}

void WordIndex::add_word(const std::string& word, uint64_t count) {
    if (word.empty() || count == 0) return;
    frequencies_[word] += count;
    total_count_ += count;
}

uint64_t WordIndex::get_frequency(std::string_view word) const {
    auto it = frequencies_.find(std::string(word));
    if (it != frequencies_.end()) {
        return it->second;
    }
    return 0;
}

uint64_t WordIndex::get_frequency(const std::string& word) const {
    auto it = frequencies_.find(word);
    if (it != frequencies_.end()) {
        return it->second;
    }
    return 0;
}

bool WordIndex::contains(std::string_view word) const {
    return frequencies_.find(std::string(word)) != frequencies_.end();
}

void WordIndex::merge(const WordIndex& other) {
    frequencies_.reserve(frequencies_.size() + other.frequencies_.size());
    for (const auto& [word, count] : other.frequencies_) {
        frequencies_[word] += count;
    }
    total_count_ += other.total_count_;
}

void WordIndex::merge(WordIndex&& other) {
    if (frequencies_.empty()) {
        frequencies_ = std::move(other.frequencies_);
        total_count_ = other.total_count_;
        other.total_count_ = 0;
        return;
    }

    frequencies_.reserve(frequencies_.size() + other.frequencies_.size());
    for (auto& [word, count] : other.frequencies_) {
        frequencies_[word] += count;
    }
    total_count_ += other.total_count_;
    other.frequencies_.clear();
    other.total_count_ = 0;
}

void WordIndex::clear() noexcept {
    frequencies_.clear();
    total_count_ = 0;
}

bool WordIndex::operator==(const WordIndex& other) const noexcept {
    if (total_count_ != other.total_count_) return false;
    if (frequencies_.size() != other.frequencies_.size()) return false;
    return frequencies_ == other.frequencies_;
}

} // namespace indexer
