// ============================================================================
// ThreadPool — fixed-size pool of worker threads backed by a shared task queue.
//
// Template parameter:
//   Task  The callable type stored in the queue.  Defaults to
//         std::function<void()>, which works with submit<>() out of the box.
//         Custom task types must themselves be callable with no arguments.
//
// Typical use (default Task):
//   ThreadPool<> pool(4);
//   auto f = pool.submit([]{ return 42; });
//   std::cout << f.get();   // 42
//
// Custom task type:
//   struct MyTask { void operator()(); ... };
//   ThreadPool<MyTask> pool(4);
//   pool.enqueue(MyTask{...});
//
// Thread safety: submit(), enqueue(), and the destructor are safe to call
//   concurrently from any thread.
// ============================================================================
#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

template<typename Task = std::function<void()>>
class ThreadPool {
public:
    // Starts `thread_count` worker threads.  If zero is passed, falls back to
    // std::thread::hardware_concurrency() (at least 1).
    explicit ThreadPool(std::size_t thread_count = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // Wrap any callable in a packaged_task, push it onto the queue, and
    // return a future for the result.  Only participates in overload resolution
    // when Task is constructible from std::function<void()> (the default).
    template<typename F, typename... Args,
             typename = std::enable_if_t<
                 std::is_constructible_v<Task, std::function<void()>>>>
    [[nodiscard]] auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // Push a pre-formed Task directly.  Useful for custom Task types.
    void enqueue(Task task);

    // Number of worker threads.
    [[nodiscard]] std::size_t size() const noexcept { return workers_.size(); }

    // Number of tasks currently waiting in the queue.
    [[nodiscard]] std::size_t pending() const noexcept {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

private:
    void workerLoop();

    std::vector<std::thread>   workers_;
    std::queue<Task>           queue_;
    mutable std::mutex         mutex_;
    std::condition_variable    cv_;
    bool                       stop_{false};
};

// ── Implementation ────────────────────────────────────────────────────────────

template<typename Task>
ThreadPool<Task>::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0)
        thread_count = std::max(1u, std::thread::hardware_concurrency());
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i)
        workers_.emplace_back([this]{ workerLoop(); });
}

template<typename Task>
ThreadPool<Task>::~ThreadPool() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_)
        w.join();
}

template<typename Task>
template<typename F, typename... Args, typename>
auto ThreadPool<Task>::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using Ret = std::invoke_result_t<F, Args...>;

    // Wrap in a shared packaged_task so the lambda pushed onto the queue can
    // be copied into std::function (packaged_task itself is move-only).
    auto pt = std::make_shared<std::packaged_task<Ret()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    auto future = pt->get_future();
    enqueue(Task{[pt]{ (*pt)(); }});
    return future;
}

template<typename Task>
void ThreadPool<Task>::enqueue(Task task) {
    {
        std::lock_guard lock(mutex_);
        if (stop_)
            throw std::runtime_error("ThreadPool::enqueue on stopped pool");
        queue_.push(std::move(task));
    }
    cv_.notify_one();
}

template<typename Task>
void ThreadPool<Task>::workerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this]{ return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop();
        }
        task();
    }
}
