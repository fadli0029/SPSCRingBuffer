/**
 * @file SPSCRingBuffer.hpp
 * @brief A fast lock-free spsc ring buffer/circular queue in modern C++.
 * @author Fadli Arsani <fadlialim0029@gmail.com>
 * @version 1.0.0
 *
 * MIT License
 *
 * Copyright (c) 2025 Fadli Arsani
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <atomic>
#include <optional>
#include <type_traits>

namespace fadli {

/**
 * @brief Lock-free single-producer single-consumer ring buffer
 * @tparam T The type of elements stored in the ring buffer
 * @warning This class is NOT thread-safe for multiple producers or consumers.
 *          Use appropriate sync. or consider MPSC variants for such cases.
 * @code
 * #include <fadli/SPSCRingBuffer.hpp>
 *
 * fadli::SPSCRingBuffer<int> buffer(1024);
 *
 * // Producer thread
 * if (buffer.try_push(42)) {
 *     // Success - completed in bounded time
 * }
 *
 * // Consumer thread
 * if (auto item = buffer.try_pop()) {
 *     // Process *item - also completed in bounded time
 * }
 * @endcode
 */
template <typename T> class SPSCRingBuffer {
    static_assert(std::is_move_constructible_v<T>,
                  "T must be move constructible");
    static_assert(std::is_move_assignable_v<T>, "T must be move assignable");

  private:
    // Helper function to round up to next power of 2
    static constexpr std::size_t next_power_of_2(std::size_t n) noexcept {
        if (n == 0)
            return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    std::size_t capacity_;   ///< Actual capacity (power of 2)
    std::size_t index_mask_; ///< Mask for fast modulo (capacity - 1)
    T* buffer_;              ///< Dynamically allocated buffer

    // Separate cache lines to prevent false sharing between producer and
    // consumer
    alignas(64) std::atomic<std::size_t> head_{0}; ///< Consumer index
    alignas(64) std::size_t tail_cache_{0};        ///< Cached tail for consumer
    alignas(64) std::atomic<std::size_t> tail_{0}; ///< Producer index
    alignas(64) std::size_t head_cache_{0};        ///< Cached head for producer

  public:
    using value_type = T;
    using size_type = std::size_t;

    /**
     * @brief Constructs a ring buffer with the specified capacity
     * @param capacity Desired capacity (will be rounded up to power of 2)
     * @throws std::bad_alloc If memory allocation fails
     */
    explicit SPSCRingBuffer(size_type capacity)
        : capacity_(next_power_of_2(capacity > 0 ? capacity : 1)),
          index_mask_(capacity_ - 1), buffer_(new T[capacity_]) {
        // Ensure capacity is reasonable
        if (capacity_ < 2) {
            capacity_ = 2;
            index_mask_ = 1;
        }
    }

    /**
     * @brief Destructor
     */
    ~SPSCRingBuffer() {
        delete[] buffer_;
    }

    // Non-copyable and non-movable for safety
    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer(SPSCRingBuffer&&) = delete;
    SPSCRingBuffer& operator=(SPSCRingBuffer&&) = delete;

    /**
     * @brief Attempt to push an element (copy version)
     * @param item The element to add to the buffer
     * @return true if the element was successfully added, false if buffer full
     * @note This function should only be called from the producer thread
     */
    [[nodiscard]] bool
    try_push(const T& item) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        const auto current_tail = tail_.load(std::memory_order_relaxed);
        const auto next_tail = (current_tail + 1) & index_mask_;

        if (next_tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next_tail == head_cache_) {
                return false;
            }
        }

        buffer_[current_tail] = item;

        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    /**
     * @brief Attempt to push an element (move version)
     * @param item The element to move into the buffer
     * @return true if the element was successfully added, false if buffer full
     * @note This function should only be called from the producer thread
     */
    [[nodiscard]] bool
    try_push(T&& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const auto current_tail = tail_.load(std::memory_order_relaxed);
        const auto next_tail = (current_tail + 1) & index_mask_;

        if (next_tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next_tail == head_cache_) {
                return false;
            }
        }

        buffer_[current_tail] = std::move(item);

        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    /**
     * @brief Get pointer to front element without removing it
     * @return Pointer to front element, or nullptr if buffer is empty
     * @note This function should only be called from the consumer thread
     * @note You must call pop() after processing the element
     */
    [[nodiscard]] T* front() noexcept {
        const auto current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (current_head == tail_cache_) {
                return nullptr;
            }
        }

        return &buffer_[current_head];
    }

    /**
     * @brief Remove the front element
     * @note This function should only be called from the consumer thread
     */
    void pop() noexcept {
        const auto current_head = head_.load(std::memory_order_relaxed);
        head_.store((current_head + 1) & index_mask_,
                    std::memory_order_release);
    }

    /**
     * @brief Attempt to pop an element
     * @return std::optional containing the popped element if successful,
     *         std::nullopt if buffer is empty
     * @note This function should only be called from the consumer thread
     * @note For better performance in tight loops, prefer using front()/pop()
     */
    [[nodiscard]] std::optional<T>
    try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const auto current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (current_head == tail_cache_) {
                return std::nullopt;
            }
        }

        T item = std::move(buffer_[current_head]);

        head_.store((current_head + 1) & index_mask_,
                    std::memory_order_release);
        return item;
    }

    /**
     * @brief Check if the buffer appears empty
     * @return true if the buffer appears empty at the time of the call
     * @note This is an approximate check due to concurrent access. The state
     *       may change immediately after this function returns.
     */
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Check if the buffer appears full
     * @return true if the buffer appears full at the time of the call
     * @note This is an approximate check due to concurrent access. The state
     *       may change immediately after this function returns.
     */
    [[nodiscard]] bool full() const noexcept {
        const auto next_tail =
            (tail_.load(std::memory_order_relaxed) + 1) & index_mask_;
        return next_tail == head_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get the approximate current size
     * @return The approximate number of elements currently in the buffer
     * @note This is an approximate value due to concurrent access. The actual
     *       size may change immediately after this function returns.
     */
    [[nodiscard]] size_type size() const noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_relaxed);
        return (tail - head) & index_mask_;
    }

    /**
     * @brief Get the maximum capacity
     * @return The maximum number of elements this buffer can hold
     * @note Due to the implementation keeping one slot empty, this returns
     *       capacity_ - 1, which is the actual usable capacity.
     */
    [[nodiscard]] size_type capacity() const noexcept {
        return capacity_ - 1; // One slot kept empty for empty/full distinction
    }

    /**
     * @brief Get the total buffer size
     * @return The total number of slots in the internal buffer
     * @note This is primarily for debugging/testing purposes.
     */
    [[nodiscard]] size_type buffer_size() const noexcept {
        return capacity_;
    }
};

} // namespace fadli
