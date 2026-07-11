#ifndef PARALLEL_UTILS_H
#define PARALLEL_UTILS_H

#include <atomic>
#include <cstdlib>
#include <exception>
#include <thread>
#include <vector>

// wasm默认无线程；以-pthread构建（需要站点开启COOP/COEP以获得SharedArrayBuffer）
// 时定义__EMSCRIPTEN_PTHREADS__，此时与原生同样支持并行。
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define SEKAI_ENGINE_SINGLE_THREADED 1
#endif

namespace engine_parallel_detail {
inline std::atomic<int>& threadOverride() {
    static std::atomic<int> value{0}; // 0 = 未设置，走环境变量
    return value;
}
inline thread_local bool insideParallelRegion = false;
}

// 运行时设置引擎并行度（绑定层暴露给无环境变量的宿主，如浏览器）。
// 传0恢复为环境变量DECK_ENGINE_THREADS的行为。
inline void setEngineThreadCount(int threads) {
    engine_parallel_detail::threadOverride().store(threads < 0 ? 0 : threads);
}

// 引擎内并行度：默认1（完全串行，行为与历史一致），由部署方通过
// DECK_ENGINE_THREADS环境变量或setEngineThreadCount按核数预算显式开启。
inline int engineThreadCount() {
#ifdef SEKAI_ENGINE_SINGLE_THREADED
    return 1;
#else
    int override = engine_parallel_detail::threadOverride().load();
    int requested = override;
    if (requested <= 0) {
        const char* env = std::getenv("DECK_ENGINE_THREADS");
        if (env == nullptr || *env == '\0') {
            return 1;
        }
        requested = std::atoi(env);
    }
    unsigned hardware = std::thread::hardware_concurrency();
    int cap = hardware > 0 ? int(hardware) : 1;
    return std::max(1, std::min(requested, cap));
#endif
}

/**
 * 对 [0, count) 的独立任务并行执行。
 * 任务体必须互不依赖且不写共享状态（结果写入按下标预分配的槽位）。
 * 任务数低于阈值、并行度为1、或已处于并行区内（防嵌套超订）时
 * 退化为串行循环，保证与串行实现逐位一致。
 * 工作线程中的异常收集后在调用线程重抛。
 */
template <typename Fn>
inline void parallelFor(std::size_t count, const Fn& fn, std::size_t minTasksPerThread = 16) {
    int threadCount = engineThreadCount();
    if (threadCount <= 1
        || engine_parallel_detail::insideParallelRegion
        || count < std::size_t(threadCount) * minTasksPerThread) {
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
            engine_parallel_detail::insideParallelRegion = true;
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
