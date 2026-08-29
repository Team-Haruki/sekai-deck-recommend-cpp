#ifndef PARALLEL_UTILS_H
#define PARALLEL_UTILS_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <pthread.h>
#endif

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
inline bool& insideParallelRegion() {
    static thread_local bool value = false;
    return value;
}

#ifndef SEKAI_ENGINE_SINGLE_THREADED

// 常驻线程池：消除每次并行分发的线程创建/join开销（细粒度搜索阶段每代都分发，
// 现场起线程的固定成本会让小请求开线程反而变慢）。
// 生命周期：首次使用时惰性创建，进程退出时线程随进程结束（对象刻意不析构，
// 规避静态析构顺序问题）；fork后子进程中指针复位、按需重建。
class EngineThreadPool {
public:
    // 任务体的类型擦除（模板trampoline，不经过std::function避免分配）
    using TaskFn = void (*)(const void* context, std::size_t index);

    // 尝试用调用线程+池内worker执行 [0,count) 的任务。
    // 池被其他并行区占用时返回false，调用方就地串行执行——
    // 并发请求争池时这正是避免超订的期望行为。
    bool tryRun(TaskFn fn, const void* context, std::size_t count, int requestedThreads) {
        std::unique_lock<std::mutex> dispatchLock(dispatchMutex, std::try_to_lock);
        if (!dispatchLock.owns_lock()) {
            return false;
        }

        int helperTarget = requestedThreads - 1; // 调用线程本身参与执行
        ensureWorkers(helperTarget);

        {
            std::lock_guard<std::mutex> lock(jobMutex);
            jobFn = fn;
            jobContext = context;
            jobCount = count;
            jobNextIndex.store(0, std::memory_order_relaxed);
            jobHelperBudget.store(helperTarget, std::memory_order_relaxed);
            jobActiveHelpers.store(0, std::memory_order_relaxed);
            jobError = nullptr;
            jobGeneration++;
        }
        jobAvailable.notify_all();

        // 调用线程作为其中一个worker参与
        runIndices();

        // 等待helper全部退出任务
        {
            std::unique_lock<std::mutex> lock(jobMutex);
            jobDone.wait(lock, [&] {
                return jobNextIndex.load(std::memory_order_relaxed) >= jobCount
                    && jobActiveHelpers.load(std::memory_order_acquire) == 0;
            });
            jobFn = nullptr;
            if (jobError) {
                auto error = jobError;
                jobError = nullptr;
                std::rethrow_exception(error);
            }
        }
        return true;
    }

private:
    void ensureWorkers(int target) {
        std::lock_guard<std::mutex> lock(jobMutex);
        unsigned hardware = std::thread::hardware_concurrency();
        int cap = hardware > 1 ? int(hardware) - 1 : 0;
        target = std::min(target, cap);
        while (spawnedWorkers < target) {
            spawnedWorkers++;
            std::thread([this] { workerLoop(); }).detach();
        }
    }

    void runIndices() {
        while (true) {
            auto index = jobNextIndex.fetch_add(1, std::memory_order_relaxed);
            if (index >= jobCount) {
                break;
            }
            try {
                jobFn(jobContext, index);
            } catch (...) {
                std::lock_guard<std::mutex> lock(jobMutex);
                if (!jobError) {
                    jobError = std::current_exception();
                }
            }
        }
    }

    void workerLoop() {
        insideParallelRegion() = true;
        uint64_t seenGeneration = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(jobMutex);
                jobAvailable.wait(lock, [&] { return jobGeneration != seenGeneration; });
                seenGeneration = jobGeneration;
                if (jobFn == nullptr) {
                    continue;
                }
                // 超出本次任务预算的worker不参与（并行度钳制到当前设置）
                if (jobHelperBudget.fetch_sub(1, std::memory_order_relaxed) <= 0) {
                    continue;
                }
                jobActiveHelpers.fetch_add(1, std::memory_order_relaxed);
            }
            runIndices();
            {
                // 修改与jobDone.wait谓词相关的状态必须持有同一把锁，
                // 否则会与"caller已评估谓词但尚未挂起"竞争造成丢失唤醒
                std::lock_guard<std::mutex> lock(jobMutex);
                jobActiveHelpers.fetch_sub(1, std::memory_order_release);
            }
            jobDone.notify_all();
        }
    }

    std::mutex dispatchMutex;        // 同一时刻只服务一个并行区
    std::mutex jobMutex;
    std::condition_variable jobAvailable;
    std::condition_variable jobDone;
    TaskFn jobFn = nullptr;
    const void* jobContext = nullptr;
    std::size_t jobCount = 0;
    std::atomic<std::size_t> jobNextIndex{0};
    std::atomic<int> jobHelperBudget{0};
    std::atomic<int> jobActiveHelpers{0};
    std::exception_ptr jobError = nullptr;
    uint64_t jobGeneration = 0;
    int spawnedWorkers = 0;
};

inline std::atomic<EngineThreadPool*>& poolPointer() {
    static std::atomic<EngineThreadPool*> pointer{nullptr};
    return pointer;
}

inline EngineThreadPool& pool() {
    auto& pointer = poolPointer();
    auto* existing = pointer.load(std::memory_order_acquire);
    if (existing != nullptr) {
        return *existing;
    }
    auto* created = new EngineThreadPool();   // 刻意不析构（进程生命周期）
    EngineThreadPool* expected = nullptr;
    if (!pointer.compare_exchange_strong(expected, created, std::memory_order_acq_rel)) {
        delete created;
        return *expected;
    }
#if !defined(_WIN32)
    // fork后的子进程里池线程不存在且内部状态不可信：复位指针，按需重建
    static bool atforkRegistered = [] {
        ::pthread_atfork(nullptr, nullptr, [] {
            engine_parallel_detail::poolPointer().store(nullptr, std::memory_order_release);
        });
        return true;
    }();
    (void)atforkRegistered;
#endif
    return *created;
}

#endif // !SEKAI_ENGINE_SINGLE_THREADED

} // namespace engine_parallel_detail

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
    int threadOverride = engine_parallel_detail::threadOverride().load();
    int requested = threadOverride;
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
 * 任务数低于阈值、并行度为1、已处于并行区内（防嵌套超订）、
 * 或常驻线程池正被其他并行区占用时，退化为串行循环，
 * 保证与串行实现逐位一致。
 * 工作线程中的异常收集后在调用线程重抛。
 */
template <typename Fn>
inline void parallelFor(std::size_t count, const Fn& fn, std::size_t minTasksPerThread = 16) {
#ifdef SEKAI_ENGINE_SINGLE_THREADED
    for (std::size_t i = 0; i < count; ++i) {
        fn(i);
    }
#else
    int threadCount = engineThreadCount();
    if (threadCount <= 1
        || engine_parallel_detail::insideParallelRegion()
        || count < std::size_t(threadCount) * minTasksPerThread) {
        for (std::size_t i = 0; i < count; ++i) {
            fn(i);
        }
        return;
    }

    engine_parallel_detail::insideParallelRegion() = true;
    bool dispatched = false;
    try {
        dispatched = engine_parallel_detail::pool().tryRun(
            [](const void* context, std::size_t index) {
                (*static_cast<const Fn*>(context))(index);
            },
            &fn, count, threadCount
        );
    } catch (...) {
        engine_parallel_detail::insideParallelRegion() = false;
        throw;
    }
    engine_parallel_detail::insideParallelRegion() = false;
    if (!dispatched) {
        for (std::size_t i = 0; i < count; ++i) {
            fn(i);
        }
    }
#endif
}

#endif // PARALLEL_UTILS_H
