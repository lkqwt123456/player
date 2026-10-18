#include "core/BoundedQueue.h"
#include <atomic>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
int main() {
    try {
        flux::BoundedQueue<int> queue(7, 28);
        std::atomic<long long> sum{0}; std::atomic<int> count{0};
        std::vector<std::thread> consumers;
        for (int n = 0; n < 3; ++n) consumers.emplace_back([&] {
            int value;
            while (!queue.drained()) if (queue.popFor(value, 50ms)) { sum += value; ++count; }
        });
        std::vector<std::thread> producers;
        for (int n = 0; n < 4; ++n) producers.emplace_back([&, n] { for (int i = 1; i <= 1000; ++i) queue.push(n * 1000 + i, 4); });
        for (auto& thread : producers) thread.join(); queue.close();
        for (auto& thread : consumers) thread.join();
        require(count == 4000 && sum == 4000LL * 4001 / 2, "Concurrent producer/consumer lost or duplicated items");
        require(!queue.push(1, 1), "Closed queue accepted data");

        flux::BoundedQueue<int> bounded(10, 4); bounded.push(11, 4);
        auto blocked = std::async(std::launch::async, [&] { return bounded.push(12, 1); });
        require(blocked.wait_for(40ms) == std::future_status::timeout, "Byte limit failed to block producer");
        bounded.abort(); require(blocked.wait_for(1s) == std::future_status::ready && !blocked.get(), "Abort did not wake producer");
        require(bounded.drained() && bounded.bytes() == 0, "Abort did not discard queue");
        flux::BoundedQueue<int> empty(2, 8);
        auto reader = std::async(std::launch::async, [&] { int out; return empty.popFor(out, 5s); });
        empty.close(); require(reader.wait_for(1s) == std::future_status::ready && !reader.get(), "Close did not wake consumer");
        flux::BoundedQueue<int> drain(3, 12); drain.push(1, 4); drain.push(2, 4); drain.close();
        int out = 0; require(drain.tryPop(out) && out == 1 && drain.tryPop(out) && out == 2 && drain.drained(), "Close must preserve FIFO data");
        bool oversized = false; try { drain.push(3, 20); } catch (const std::length_error&) { oversized = true; }
        require(oversized, "Oversized item did not fail immediately");
        flux::BoundedQueue<std::unique_ptr<int>> ownership(1, 8); ownership.push(std::make_unique<int>(42), 4);
        std::unique_ptr<int> moved; require(ownership.tryPop(moved) && *moved == 42, "Move-only ownership failed");
        flux::BoundedQueue<int> timeout(1, 4); const auto began = std::chrono::steady_clock::now();
        require(!timeout.popFor(out, 30ms), "Empty read unexpectedly succeeded");
        require(std::chrono::steady_clock::now() - began >= 25ms, "Timed read returned too early");
        std::cout << "PASS: concurrency, byte bounds, close/drain, abort, ownership, timeout\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
