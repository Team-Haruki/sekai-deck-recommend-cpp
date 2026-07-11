#ifndef PARALLEL_UTILS_H
#define PARALLEL_UTILS_H

#include <atomic>
#include <cstdlib>
#include <exception>
#include <thread>
#include <vector>

// 引擎内并行度：默认1（完全串行，行为与历史一致），由部署方通过
// DECK_ENGINE_THREADS 按核数预算显式开启。wasm无线程环境强制串行。
inline int engineThreadCount() {
#ifdef __EMSCRIPTEN__
    return 1;
#else
    static const int cached = [] {
        const char* env = std::getenv("DECK_ENGINE_THREADS");
        if (env == nullptr || *env == '\0') {
            return 1;
        }
        int requested = std::atoi(env);
        unsigned hardware = std::thread::hardware_concurrency();
        int cap = hardware > 0 ? int(hardware) : 1;
        return std::max(1, std::min(requested, cap));
    }();
    return cached;
#endif
}

/**
 * 对 [0, count) 的独立任务并行执行。
 * 任务体必须互不依赖且不写共享状态（结果写入按下标预分配的槽位）。
 * 任务数低于阈值或并行度为1时退化为串行循环，保证与串行实现逐位一致。
 * 工作线程中的异常收集后在调用线程重抛。
 */
template <typename Fn>
inline void parallelFor(std::size_t count, const Fn& fn, std::size_t minTasksPerThread = 16) {
    int threadCount = engineThreadCount();
    if (threadCount <= 1 || count < std::size_t(threadCount) * minTasksPerThread) {
        for (std::size_t i = 0; i < count; ++i) {
            fn(i);
        }
        return;
    }

    std::atomic<std::size_t> nextTask{0};
    std::vector<std::exception_ptr> workerErrors(threadCount);
    std::vector<std::thread> workers{};
    workers.reserve(threadCount);
    for (int t = 0; t < threadCount; ++t) {
        workers.emplace_back([&, t] {
            try {
                while (true) {
                    auto index = nextTask.fetch_add(1);
                    if (index >= count) {
                        break;
                    }
                    fn(index);
                }
            } catch (...) {
                workerErrors[t] = std::current_exception();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (auto& error : workerErrors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

#endif // PARALLEL_UTILS_H
