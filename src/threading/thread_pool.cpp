#include "threading/thread_pool.h"
#include <iostream>

namespace threading {

ThreadPool::ThreadPool(
    size_t num_threads,
    size_t max_queue_size,
    BackpressurePolicy policy,
    std::chrono::milliseconds enqueue_timeout
) : max_queue_size_(max_queue_size),
    policy_(policy),
    enqueue_timeout_(enqueue_timeout) {
    if (num_threads == 0) {
        num_threads = std::max(1u, std::thread::hardware_concurrency());
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown(false);
}

void ThreadPool::shutdown(bool wait_for_all_tasks) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return;
        stop_.store(true);
    }

    cv_task_available_.notify_all();
    cv_space_available_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    if (!wait_for_all_tasks) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        while (!tasks_.empty()) {
            tasks_.pop();
        }
    }
}

bool ThreadPool::enqueue(std::function<void()> task) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    if (stop_.load()) {
        rejected_tasks_++;
        return false;
    }

    if (tasks_.size() >= max_queue_size_) {
        if (policy_ == BackpressurePolicy::Reject) {
            rejected_tasks_++;
            return false;
        } else { // BlockWithTimeout
            bool space_ready = cv_space_available_.wait_for(
                lock, enqueue_timeout_, [this]() {
                    return stop_.load() || tasks_.size() < max_queue_size_;
                }
            );

            if (!space_ready || stop_.load() || tasks_.size() >= max_queue_size_) {
                rejected_tasks_++;
                return false;
            }
        }
    }

    tasks_.push(std::move(task));
    cv_task_available_.notify_one();
    return true;
}

size_t ThreadPool::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_task_available_.wait(lock, [this]() {
                return stop_.load() || !tasks_.empty();
            });

            if (stop_.load() && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            cv_space_available_.notify_one();
        }

        active_workers_++;
        try {
            task();
        } catch (const std::exception& ex) {
            std::cerr << "[ThreadPool Worker Error] " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "[ThreadPool Worker Error] Unknown exception caught." << std::endl;
        }
        active_workers_--;
        completed_tasks_++;
    }
}

} // namespace threading
