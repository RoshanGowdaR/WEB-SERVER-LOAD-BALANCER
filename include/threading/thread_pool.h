#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <chrono>

namespace threading {

enum class BackpressurePolicy {
    Reject,
    BlockWithTimeout
};

class ThreadPool {
public:
    explicit ThreadPool(
        size_t num_threads,
        size_t max_queue_size = 10000,
        BackpressurePolicy policy = BackpressurePolicy::BlockWithTimeout,
        std::chrono::milliseconds enqueue_timeout = std::chrono::milliseconds(500)
    );

    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Enqueue a task into the thread pool.
     * @return true if enqueued, false if rejected due to queue capacity.
     */
    bool enqueue(std::function<void()> task);

    /**
     * @brief Enqueue a task returning a std::future.
     */
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        bool enqueued = enqueue([task]() { (*task)(); });
        if (!enqueued) {
            throw std::runtime_error("ThreadPool queue is full and rejected the task");
        }
        return res;
    }

    void shutdown(bool wait_for_all_tasks = true);

    [[nodiscard]] size_t worker_count() const noexcept { return workers_.size(); }
    [[nodiscard]] size_t queue_size() const;
    [[nodiscard]] size_t active_workers() const noexcept { return active_workers_.load(); }
    [[nodiscard]] uint64_t completed_tasks() const noexcept { return completed_tasks_.load(); }
    [[nodiscard]] uint64_t rejected_tasks() const noexcept { return rejected_tasks_.load(); }
    [[nodiscard]] bool is_shutdown() const noexcept { return stop_.load(); }

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex queue_mutex_;
    std::condition_variable cv_task_available_;
    std::condition_variable cv_space_available_;

    size_t max_queue_size_;
    BackpressurePolicy policy_;
    std::chrono::milliseconds enqueue_timeout_;

    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_workers_{0};
    std::atomic<uint64_t> completed_tasks_{0};
    std::atomic<uint64_t> rejected_tasks_{0};
};

} // namespace threading
