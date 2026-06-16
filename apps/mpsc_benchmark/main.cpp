#include <iostream>
#include <atomic>
#include <chrono>
#include <array>
#include <vector>
#include <numeric>
#include <algorithm>
#include <thread>
#include "mpsc_buffer/MpscQueue.h"

using namespace mpsc_buffer;

#pragma pack(push, 1)
/**
 * @brief High-velocity command message structure.
 * Simulates a standard inbound exchange routing order token.
 */
struct OrderCommand {
    uint64_t timestampNs;
    uint64_t accountId;
    uint32_t orderId;
    uint32_t price;
    uint32_t quantity;
    char     side;        // 'B' = Buy, 'S' = Sell
    char     commandType; // 'N' = New, 'C' = Cancel
};
#pragma pack(pop)

// Benchmark Scaling Rules
inline constexpr size_t kNumProducers = 4;
inline constexpr size_t kMessagesPerProducer = 250'000;
inline constexpr size_t kTotalMessages = kNumProducers * kMessagesPerProducer;

namespace {
    std::atomic<size_t> g_receivedCount{0};
    std::atomic<bool> g_startSignal{false};
}

int main() {
    // 1. Allocate the Multi-Producer Single-Consumer Queue (Capacity must be a power of 2)
    MpscQueue<OrderCommand, 16384> commandQueue;

    // 2. Setup and Launch the Single Dedicated Consumer Thread (The Matching Engine Core)
    std::jthread consumerThread([&commandQueue](std::stop_token stopToken) {
        // Hard-pin the matching consumer core onto isolated hardware Core 2
        pinCurrentThread(2);

        OrderCommand cmd;
        size_t processed = 0;

        while (!stopToken.stop_requested() && processed < kTotalMessages) {
            if (commandQueue.tryPop(cmd)) {
                // Simulating hot-path execution sequence bookkeeping
                [[maybe_unused]] uint32_t activeId = cmd.orderId;
                [[maybe_unused]] uint32_t activePrice = cmd.price;
                
                ++processed;
                g_receivedCount.store(processed, std::memory_order_relaxed);
            } else {
                cpuPause(); // Minimize execution line serialization stalls on empty passes
            }
        }
    });

    // 3. Setup and Launch the 4 Concurrent Producer Threads (Order Entry Gateways)
    std::vector<std::jthread> producerThreads;
    producerThreads.reserve(kNumProducers);

    std::cout << "--> Spinning up " << kNumProducers << " parallel gateway producer threads..." << std::endl;
    for (size_t t = 0; t < kNumProducers; ++t) {
        producerThreads.emplace_back([&commandQueue, t]() {
            // Assign unique hardware core mappings sequentially (Cores 4, 5, 6, 7) to avoid core sharing
            pinCurrentThread(static_cast<int>(4 + t));

            // Wait for the global start signal so all threads slam the atomic queue simultaneously
            while (!g_startSignal.load(std::memory_order_relaxed)) {
                cpuPause();
            }

            for (size_t i = 0; i < kMessagesPerProducer; ++i) {
                OrderCommand cmd{
                    .timestampNs = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()),
                    .accountId = 9000ULL + t,
                    .orderId = static_cast<uint32_t>(t * kMessagesPerProducer + i),
                    .price = 10050 + static_cast<uint32_t>(i % 5),
                    .quantity = 10,
                    .side = (i % 2 == 0) ? 'B' : 'S',
                    .commandType = 'N'
                };

                // Atomic multi-writer contention loop: push returns false if slot sequence checks fail (queue full)
                while (!commandQueue.tryPush(cmd)) {
                    cpuPause();
                }
            }
        });
    }

    // Allow core affinities and thread contexts to settle completely
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "--> Dropping start gate. Commencing atomic contention blast (1M commands)..." << std::endl;
    auto startWallTime = std::chrono::steady_clock::now();
    
    // Trigger global thread coordination release
    g_startSignal.store(true, std::memory_order_release);

    // Block main thread until the single consumer core processes all 1M payloads
    while (g_receivedCount.load(std::memory_order_relaxed) < kTotalMessages) {
        cpuPause();
    }

    auto endWallTime = std::chrono::steady_clock::now();

    // Clean up worker scopes safely via modern jthread lifecycles
    consumerThread.request_stop();
    for (auto& thr : producerThreads) {
        thr.join();
    }

    // 4. Compute and Render Multi-Writer System Throughput Telemetry
    auto totalDurationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endWallTime - startWallTime).count();
    auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endWallTime - startWallTime).count();
    
    double throughput = (static_cast<double>(kTotalMessages) / (static_cast<double>(totalDurationMs) / 1000.0)) / 1e6;
    double avgLatencyPerRecord = static_cast<double>(totalDurationNs) / kTotalMessages;

    std::cout << "\n====================================================\n"
              << "    LOCK-FREE MPSC QUEUE CONTENTION PROFILE         \n"
              << "====================================================\n"
              << "  Total Commands Enqueued    : " << kTotalMessages << "\n"
              << "  Concurrent Writers (Gways) : " << kNumProducers << "\n"
              << "  Serialization Throughput   : " << throughput << " Million commands/sec\n"
              << "  Average Handoff Cycle Time : " << avgLatencyPerRecord << " ns/msg\n"
              << "====================================================\n" << std::endl;

    return 0;
}