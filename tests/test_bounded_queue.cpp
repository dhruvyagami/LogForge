#include "test_framework.hpp"
#include <indexer/concurrency/bounded_queue.hpp>
#include <thread>
#include <vector>
#include <atomic>

using namespace indexer::concurrency;

TEST_CASE(BoundedQueueBasicOperations) {
    BoundedQueue<int> q(2);
    ASSERT_TRUE(q.empty());
    ASSERT_EQ(q.size(), 0u);

    ASSERT_TRUE(q.push(10));
    ASSERT_TRUE(q.push(20));
    ASSERT_EQ(q.size(), 2u);

    auto item1 = q.pop();
    ASSERT_TRUE(item1.has_value());
    ASSERT_EQ(*item1, 10);

    auto item2 = q.pop();
    ASSERT_TRUE(item2.has_value());
    ASSERT_EQ(*item2, 20);

    ASSERT_TRUE(q.empty());
}

TEST_CASE(BoundedQueueCloseSemantics) {
    BoundedQueue<int> q(5);
    q.push(1);
    q.push(2);
    q.close();

    // Push should fail once closed
    ASSERT_FALSE(q.push(3));

    // Pop should drain existing items
    auto v1 = q.pop();
    ASSERT_TRUE(v1.has_value());
    ASSERT_EQ(*v1, 1);

    auto v2 = q.pop();
    ASSERT_TRUE(v2.has_value());
    ASSERT_EQ(*v2, 2);

    // Queue is empty and closed, should return nullopt
    auto v3 = q.pop();
    ASSERT_FALSE(v3.has_value());
}

TEST_CASE(BoundedQueueMultiProducerConsumer) {
    constexpr size_t num_producers = 4;
    constexpr size_t num_consumers = 4;
    constexpr size_t items_per_producer = 2500;
    constexpr size_t total_items = num_producers * items_per_producer;

    BoundedQueue<int> q(50);
    std::atomic<uint64_t> sum_consumed{0};
    std::atomic<size_t> items_consumed{0};

    std::vector<std::thread> consumers;
    for (size_t i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&q, &sum_consumed, &items_consumed]() {
            while (auto item = q.pop()) {
                sum_consumed.fetch_add(*item, std::memory_order_relaxed);
                items_consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<std::thread> producers;
    for (size_t i = 0; i < num_producers; ++i) {
        producers.emplace_back([&q]() {
            for (size_t j = 0; j < items_per_producer; ++j) {
                q.push(1);
            }
        });
    }

    for (auto& p : producers) {
        p.join();
    }

    // Producers are done, signal closure
    q.close();

    for (auto& c : consumers) {
        c.join();
    }

    ASSERT_EQ(items_consumed.load(), total_items);
    ASSERT_EQ(sum_consumed.load(), total_items * 1);
}
