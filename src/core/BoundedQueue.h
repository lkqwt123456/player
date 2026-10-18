#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace flux {
// Single ownership moves across stages. close() drains; abort() discards and wakes
// every blocked producer/consumer. Both count and payload bytes are bounded.
template<class T> class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity, std::size_t byteLimit)
        : capacity_(capacity), byteLimit_(byteLimit) {
        if (!capacity || !byteLimit) throw std::invalid_argument("Queue capacity must be positive");
    }
    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    bool push(T value, std::size_t bytes) {
        if (bytes > byteLimit_) throw std::length_error("Single media item exceeds queue byte limit");
        std::unique_lock<std::mutex> lock(mutex_);
        writable_.wait(lock, [&] { return closed_ || (items_.size() < capacity_ && bytes_ <= byteLimit_ - bytes); });
        if (closed_) return false;
        items_.push_back({std::move(value), bytes});
        bytes_ += bytes;
        readable_.notify_one();
        return true;
    }
    template<class Rep, class Period>
    bool popFor(T& value, std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!readable_.wait_for(lock, timeout, [&] { return closed_ || !items_.empty(); })) return false;
        return popLocked(value);
    }
    bool tryPop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        return popLocked(value);
    }
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        readable_.notify_all(); writable_.notify_all();
    }
    void abort() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true; items_.clear(); bytes_ = 0;
        readable_.notify_all(); writable_.notify_all();
    }
    bool drained() const { std::lock_guard<std::mutex> lock(mutex_); return closed_ && items_.empty(); }
    std::size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return items_.size(); }
    std::size_t bytes() const { std::lock_guard<std::mutex> lock(mutex_); return bytes_; }
private:
    bool popLocked(T& value) {
        if (items_.empty()) return false;
        value = std::move(items_.front().value);
        bytes_ -= items_.front().bytes; items_.pop_front();
        writable_.notify_one();
        return true;
    }
    struct Entry { T value; std::size_t bytes; };
    const std::size_t capacity_, byteLimit_;
    mutable std::mutex mutex_;
    std::condition_variable readable_, writable_;
    std::deque<Entry> items_;
    std::size_t bytes_ = 0;
    bool closed_ = false;
};
}
