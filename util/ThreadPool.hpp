#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>

class ThreadPool {
public:
    explicit ThreadPool(unsigned count = 0);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>;

    void parallelFor(size_t start, size_t end,
                     const std::function<void(size_t, size_t)>& func,
                     size_t minChunk = 1);
    unsigned threadCount() const { return (unsigned)m_workers.size(); }

private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop{false};
    void workerLoop();
};

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
    using Ret = typename std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<Ret()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<Ret> result = task->get_future();
    { std::lock_guard<std::mutex> lock(m_mutex); if(m_stop) throw std::runtime_error("ThreadPool stopped"); m_tasks.emplace([task](){(*task)();}); }
    m_cv.notify_one();
    return result;
}
