#include "test_framework.hpp"
#include <indexer/diff.hpp>

using namespace indexer;

TEST_CASE(DiffIdentical) {
    WordIndex idx1;
    idx1.add_word("linux", 10);
    idx1.add_word("kernel", 5);

    WordIndex idx2;
    idx2.add_word("linux", 10);
    idx2.add_word("kernel", 5);

    auto diff = compute_diff(idx1, idx2);
    ASSERT_TRUE(diff.is_identical());
    ASSERT_EQ(diff.total_differences(), 0u);
}

TEST_CASE(DiffAddedRemovedModified) {
    WordIndex v1;
    v1.add_word("stable", 10);
    v1.add_word("obsolete", 3);
    v1.add_word("changing", 5);

    WordIndex v2;
    v2.add_word("stable", 10);     // Unchanged
    v2.add_word("changing", 15);    // Modified (+10)
    v2.add_word("brand_new", 8);    // Added
    // "obsolete" removed

    auto diff = compute_diff(v1, v2);
    ASSERT_FALSE(diff.is_identical());

    // Added
    ASSERT_EQ(diff.added.size(), 1u);
    ASSERT_EQ(diff.added[0].word, "brand_new");
    ASSERT_EQ(diff.added[0].new_freq, 8u);

    // Removed
    ASSERT_EQ(diff.removed.size(), 1u);
    ASSERT_EQ(diff.removed[0].word, "obsolete");
    ASSERT_EQ(diff.removed[0].old_freq, 3u);

    // Modified
    ASSERT_EQ(diff.modified.size(), 1u);
    ASSERT_EQ(diff.modified[0].word, "changing");
    ASSERT_EQ(diff.modified[0].old_freq, 5u);
    ASSERT_EQ(diff.modified[0].new_freq, 15u);
    ASSERT_EQ(diff.modified[0].delta(), 10);
}
