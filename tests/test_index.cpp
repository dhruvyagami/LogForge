#include "test_framework.hpp"
#include <indexer/index.hpp>

using namespace indexer;

TEST_CASE(IndexAddAndQuery) {
    WordIndex idx;
    ASSERT_TRUE(idx.empty());
    ASSERT_EQ(idx.unique_words(), 0u);
    ASSERT_EQ(idx.total_words(), 0u);

    idx.add_word("apple", 2);
    idx.add_word("banana", 3);
    idx.add_word("apple", 1);

    ASSERT_FALSE(idx.empty());
    ASSERT_EQ(idx.unique_words(), 2u);
    ASSERT_EQ(idx.total_words(), 6u);
    ASSERT_EQ(idx.get_frequency("apple"), 3u);
    ASSERT_EQ(idx.get_frequency("banana"), 3u);
    ASSERT_EQ(idx.get_frequency("orange"), 0u);
}

TEST_CASE(IndexMerge) {
    WordIndex idx1;
    idx1.add_word("apple", 5);
    idx1.add_word("cherry", 2);

    WordIndex idx2;
    idx2.add_word("apple", 3);
    idx2.add_word("banana", 4);

    idx1.merge(idx2);

    ASSERT_EQ(idx1.unique_words(), 3u);
    ASSERT_EQ(idx1.total_words(), 14u);
    ASSERT_EQ(idx1.get_frequency("apple"), 8u);
    ASSERT_EQ(idx1.get_frequency("banana"), 4u);
    ASSERT_EQ(idx1.get_frequency("cherry"), 2u);
}

TEST_CASE(IndexMoveMerge) {
    WordIndex idx1;
    idx1.add_word("dog", 1);

    WordIndex idx2;
    idx2.add_word("dog", 2);
    idx2.add_word("cat", 5);

    idx1.merge(std::move(idx2));

    ASSERT_EQ(idx1.get_frequency("dog"), 3u);
    ASSERT_EQ(idx1.get_frequency("cat"), 5u);
    ASSERT_TRUE(idx2.empty());
}
