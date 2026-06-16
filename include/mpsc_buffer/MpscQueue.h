#pragma once

#include <utility>
#include <memory>
#include <type_traits>
#include <atomic>
#include <array>
#include <cstddef>
#include <chrono>

#include "mpsc_buffer/Platform.h"

namespace mpsc_buffer {

inline constexpr size_t DefaultMpscQueueCapacity = 1024;

/**
 * @brief Zero-allocation lock-free Multi-Producer Single-Consumer (MPSC) bounded circular ring buffer.
 * Engineered for ultra-low latency command serialization into matching engine cores.
 *
 * @tparam T Core message command payload type.
 * @tparam Capacity Slot allocation allocation line. MUST represent an explicit power of two.
 */
template <typename T, size_t Capacity = DefaultMpscQueueCapacity>
class MpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of 2");

private:
    static constexpr size_t CacheLine = getCacheLineSize();
    static constexpr size_t IndexMask = Capacity - 1;

    struct Slot {
        std::atomic<size_t> sequence;
        alignas(alignof(T)) std::byte data[sizeof(T)];
    };

public:
    MpscQueue() : _head(0), _tail(0) {
        // Initialize the slots with their natural sequential position offsets
        for (size_t i = 0; i < Capacity; ++i) {
            _buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Destructor guarantees safe unwinding by popping any remaining active elements.
     */
    ~MpscQueue() noexcept {
        T dummy;
        while (tryPop(dummy));
    }

    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;
    MpscQueue(MpscQueue&&) = delete;
    MpscQueue& operator=(MpscQueue&&) = delete;

    /**
     * @brief High-velocity Multi-Writer item enqueue operation.
     * Uses atomic ticket reservation loops to claim memory slots without cross-thread lock blocks.
     */
    template <typename U>
    [[nodiscard]] bool tryPush(U&& item) noexcept(std::is_nothrow_constructible_v<T, U>) {
        size_t head = _head.load(std::memory_order_relaxed);

        while (true) {
            auto& slot = _buffer[head & IndexMask];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            int64_t dif = static_cast<int64_t>(seq) - static_cast<int64_t>(head);

            if (dif == 0) {
                // Instantly attempt to reserve this specific slot sequence ticket across all producer threads
                if (_head.compare_exchange_weak(head, head + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    // Ticket secured! Safe to copy payload directly into uninitialized storage memory
                    T* targetPtr = (T*)(slot.data);
                    std::construct_at(targetPtr, std::forward<U>(item));
                    
                    // Flip the sequence flag to notify the consumer thread that write copy is complete
                    slot.sequence.store(head + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                // Queue is full: The consumer thread hasn't cleared out this slot layer yet
                return false;
            } else {
                // Spurious compare-exchange fail or thread preemption step: reread the head state
                head = _head.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Low-Overhead Single-Reader item dequeue operation.
     * Invoked exclusively by the single dedicated matching engine consumer thread.
     */
    [[nodiscard]] bool tryPop(T& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
        size_t tail = _tail.load(std::memory_order_relaxed);
        auto& slot = _buffer[tail & IndexMask];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        int64_t dif = static_cast<int64_t>(seq) - static_cast<int64_t>(tail + 1);

        if (dif == 0) {
            // Data is safe and fully populated by a producer!
            T* objectPtr = (T*)(slot.data);
            item = std::move(*objectPtr);
            std::destroy_at(objectPtr);

            // Roll the slot sequence tracking marker to make it claimable for the next producer layer pass
            slot.sequence.store(tail + Capacity, std::memory_order_release);
            
            // Advance the single-reader queue tracker register branchlessly
            _tail.store(tail + 1, std::memory_order_release);
            return true;
        }

        // Queue is completely empty or the producer thread is still mid-copy loop write
        return false;
    }

private:
    // 1. Thread-Isolated Matrix Backing Memory
    alignas(CacheLine) std::array<Slot, Capacity> _buffer;

    // 2. Multi-Writer Write Index Barrier Register
    alignas(CacheLine) std::atomic<size_t> _head;

    // 3. Single-Reader Read Index Barrier Register
    alignas(CacheLine) std::atomic<size_t> _tail;
};

} // namespace mpsc_buffer