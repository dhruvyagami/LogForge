#include "test_framework.hpp"
#include <indexer/topk.hpp>

using namespace indexer;

TEST_CASE(TopKEmptyAndZero) {
    WordIndex idx;
    auto res1 = get_top_k(idx, 5);
    ASSERT_TRUE(res1.empty());

    idx.add_word("hello", 10);
    auto res2 = get_top_k(idx, 0);
    ASSERT_TRUE(res2.empty());
}

TEST_CASE(TopKDeterministicTieBreaking) {
    WordIndex idx;
    // Four words with equal frequency
    idx.add_word("zebra", 10);
    idx.add_word("apple", 10);
    idx.add_word("mango", 10);
    idx.add_word("banana", 10);

    // K = 2: Should pick the two lexicographically smallest: "apple", "banana"
    auto top2 = get_top_k(idx, 2);
    ASSERT_EQ(top2.size(), 2u);
    ASSERT_EQ(top2[0].word, "apple");
    ASSERT_EQ(top2[0].frequency, 10u);
    ASSERT_EQ(top2[1].word, "banana");
    ASSERT_EQ(top2[1].frequency, 10u);
}

TEST_CASE(TopKFrequenciesDescending) {
    WordIndex idx;
    idx.add_word("rare", 1);
    idx.add_word("common", 100);
    idx.add_word("medium", 50);
    idx.add_word("abundant", 200);

    auto top3 = get_top_k(idx, 3);
    ASSERT_EQ(top3.size(), 3u);
    ASSERT_EQ(top3[0].word, "abundant");
    ASSERT_EQ(top3[0].frequency, 200u);
    ASSERT_EQ(top3[1].word, "common");
    ASSERT_EQ(top3[1].frequency, 100u);
    ASSERT_EQ(top3[2].word, "medium");
    ASSERT_EQ(top3[2].frequency, 50u);
}
