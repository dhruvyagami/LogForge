#include <indexer/diff.hpp>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace indexer {

IndexDiff compute_diff(const WordIndex& old_index, const WordIndex& new_index) {
    IndexDiff diff;

    // Check old_index entries for removals and modifications
    for (const auto& [word, old_freq] : old_index) {
        uint64_t new_freq = new_index.get_frequency(word);
        if (new_freq == 0) {
            diff.removed.push_back({word, old_freq, 0});
        } else if (new_freq != old_freq) {
            diff.modified.push_back({word, old_freq, new_freq});
        }
    }

    // Check new_index entries for additions
    for (const auto& [word, new_freq] : new_index) {
        if (!old_index.contains(word)) {
            diff.added.push_back({word, 0, new_freq});
        }
    }

    // Sort all lists lexicographically by word for deterministic output
    auto comp_word = [](const WordDiff& a, const WordDiff& b) {
        return a.word < b.word;
    };

    std::sort(diff.added.begin(), diff.added.end(), comp_word);
    std::sort(diff.removed.begin(), diff.removed.end(), comp_word);
    std::sort(diff.modified.begin(), diff.modified.end(), comp_word);

    return diff;
}

void IndexDiff::print(std::ostream& os) const {
    os << "========================================\n";
    os << "           INDEX VERSION DIFF           \n";
    os << "========================================\n";
    os << "Total Added:    " << added.size() << "\n";
    os << "Total Removed:  " << removed.size() << "\n";
    os << "Total Modified: " << modified.size() << "\n";
    os << "----------------------------------------\n";

    if (!added.empty()) {
        os << "[+] Added Words:\n";
        for (const auto& item : added) {
            os << "  + " << std::left << std::setw(24) << item.word 
               << " (count: " << item.new_freq << ")\n";
        }
    }

    if (!removed.empty()) {
        os << "[-] Removed Words:\n";
        for (const auto& item : removed) {
            os << "  - " << std::left << std::setw(24) << item.word 
               << " (was: " << item.old_freq << ")\n";
        }
    }

    if (!modified.empty()) {
        os << "[*] Frequency Changes:\n";
        for (const auto& item : modified) {
            std::string delta_str = (item.delta() > 0 ? "+" : "") + std::to_string(item.delta());
            os << "  * " << std::left << std::setw(24) << item.word 
               << " " << item.old_freq << " -> " << item.new_freq 
               << " (" << delta_str << ")\n";
        }
    }

    if (is_identical()) {
        os << "Both index snapshots are identical.\n";
    }
    os << "========================================\n";
}

} // namespace indexer
