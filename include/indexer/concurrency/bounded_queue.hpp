#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <cstddef>

namespace indexer::concurrency {

/**
 * @brief Thread-safe bounded blocking queue with clean shutdown semantics.
 * 
 * Uses std::mutex and two condition variables (not_full, not_empty).
 * Eliminates busy-waiting. When close() is invoked, all blocked threads are
 * awakened; consumers safely drain remaining elements until the queue is empty.
 */
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity)
        : capacity_(capacity > 0 ? capacity : 1) {}

    ~BoundedQueue() {
        close();
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /**
     * @brief Enqueue an item. Blocks if queue is currently full.
     * 
     * @param item Item to be enqueued.
     * @return true if successfully enqueued, false if queue was closed.
     */
    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this]() {
            return queue_.size() < capacity_ || closed_;
        });

        if (closed_) {
            return false;
        }

        queue_.push(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    /**
     * @brief Dequeue an item. Blocks if queue is empty and not closed.
     * 
     * @return std::optional<T> Containing the item, or std::nullopt if queue
     *         is closed and completely drained.
     */
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this]() {
            return !queue_.empty() || closed_;
        });

        if (queue_.empty()) {
            return std::nullopt; // Closed and fully drained
        }

        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }

    /**
     * @brief Close the queue to further pushes and wake up all waiting threads.
     */
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t capacity() const noexcept {
        return capacity_;
    }

private:
    const size_t capacity_;
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    bool closed_{false};
};

} // namespace indexer::concurrency
