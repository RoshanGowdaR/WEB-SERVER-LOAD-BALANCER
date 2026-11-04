#include <gtest/gtest.h>
#include "threading/thread_pool.h"
#include <atomic>
#include <chrono>

TEST(ThreadPoolTest, ExecuteTasksConcurrently) {
    constexpr size_t num_workers = 4;
    constexpr size_t num_tasks = 100;
    threading::ThreadPool pool(num_workers, 500);

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    EXPECT_EQ(counter.load(), static_cast<int>(num_tasks));
    EXPECT_EQ(pool.completed_tasks(), num_tasks);
}

TEST(ThreadPoolTest, BackpressureRejectionPolicy) {
    // 1 worker, tiny queue of size 2, Reject policy
    threading::ThreadPool pool(1, 2, threading::BackpressurePolicy::Reject);

    std::atomic<bool> worker_started{false};
    std::atomic<bool> block_worker{true};
    // Block the 1 worker
    pool.enqueue([&]() {
        worker_started.store(true);
        while (block_worker.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    while (!worker_started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Fill the 2 slots in queue
    EXPECT_TRUE(pool.enqueue([]() {}));
    EXPECT_TRUE(pool.enqueue([]() {}));

    // Next enqueue should be immediately rejected
    EXPECT_FALSE(pool.enqueue([]() {}));
    EXPECT_GT(pool.rejected_tasks(), 0);

    // Unblock worker to clean up
    block_worker.store(false);
    pool.shutdown(true);
}

TEST(ThreadPoolTest, WorkerCatchesTaskExceptionSafely) {
    threading::ThreadPool pool(2, 10);

    // Task that throws
    pool.enqueue([]() {
        throw std::runtime_error("Simulated catastrophic task exception");
    });

    // Subsequent task should still execute normally because worker survived
    auto fut = pool.submit([]() {
        return 42;
    });

    EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPoolTest, GracefulShutdownDrainsQueue) {
    threading::ThreadPool pool(2, 20);
    std::atomic<int> executed{0};

    for (int i = 0; i < 10; ++i) {
        pool.enqueue([&executed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            executed++;
        });
    }

    pool.shutdown(true); // Graceful: must drain all 10 tasks
    EXPECT_EQ(executed.load(), 10);
    EXPECT_TRUE(pool.is_shutdown());
}

TEST(ThreadPoolTest, ImmediateShutdownDiscardsPendingTasks) {
    threading::ThreadPool pool(1, 20);
    std::atomic<bool> worker_running{false};
    std::atomic<bool> hold_worker{true};
    std::atomic<int> executed{0};

    // First task holds the only worker
    pool.enqueue([&]() {
        worker_running.store(true);
        while (hold_worker.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        executed++;
    });

    while (!worker_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Enqueue 5 additional tasks that sit in queue
    for (int i = 0; i < 5; ++i) {
        pool.enqueue([&executed]() {
            executed++;
        });
    }

    // Shutdown immediately (false): should discard the 5 queued tasks
    hold_worker.store(false);
    pool.shutdown(false);

    // Only the first active task finishes; queued 5 are discarded
    EXPECT_EQ(executed.load(), 1);
}

TEST(ThreadPoolTest, IdempotentShutdown) {
    threading::ThreadPool pool(2, 10);
    pool.shutdown(true);
    // Repeated shutdown must not throw, deadlock, or crash
    pool.shutdown(false);
    pool.shutdown(true);
    EXPECT_TRUE(pool.is_shutdown());
}
