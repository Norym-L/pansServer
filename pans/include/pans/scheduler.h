#ifndef PANS_INCLUDE_PANS_SCHEDULER_H
#define PANS_INCLUDE_PANS_SCHEDULER_H

#include <atomic>
#include <condition_variable>
#include <concepts>
#include <functional>
#include <iosfwd>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <pans/export.h>
#include <pans/fiber.h>
#include <pans/types.h>

namespace pans
{
class PANS_API Scheduler
{
public:
    using ptr = std::shared_ptr<Scheduler>;
    using ThreadId = u64;

    static constexpr ThreadId ANY_THREAD = 0;

    explicit Scheduler(std::size_t threads = 1, bool use_caller = true, const std::string& name = "");

    virtual ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler operator=(Scheduler&&) = delete;

    [[nodiscard]] const std::string& getName() { return m_name; }
    [[nodiscard]] static Scheduler* GetThis() noexcept;
    [[nodiscard]] static Fiber* GetMainFiber() noexcept;

    void start();
    /**
     * 请求停止调度器
     */
    void stop();

    template<typename FiberOrCallback>
    void addTask(FiberOrCallback&& task, ThreadId thread_id = ANY_THREAD)
    {
        bool need_tickle = false; // 是否需要通知别的线程来执行
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            need_tickle = scheduleNoLock(std::forward<FiberOrCallback>(task), thread_id); // 是否需要唤醒线程
        }
        if (need_tickle)
        {
            tickle(); // 唤醒工作线程
        }
    }

    template<typename InputIterator>
    void addTask(InputIterator begin, InputIterator end)
    {
        bool need_tickle = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (begin != end)
            {
                need_tickle = scheduleNoLock(*begin, ANY_THREAD) || need_tickle;
                ++begin;
            }
        }
        if (need_tickle)
        {
            tickle();
        }
    }

    void switchTo(ThreadId thread_id = ANY_THREAD);

    std::ostream& dump(std::ostream& output) const;

private:
    /// 唤醒线程
    virtual void tickle();

    /// 开启调度器主循环
    void run();

    /// 调度器是否满足退出条件
    [[nodiscard]] virtual bool canStop();

    /// 空闲调度器让出CPU
    virtual void idle();

    /// 标记当前线程的调度器
    void setThis() noexcept;

    /// 是否有空闲线程
    [[nodiscard]] bool hasIdleThreads() const noexcept
    {
        return m_idleThreadCount.load(std::memory_order_acquire) != 0;
    }

private:
    using Callback = std::function<void()>;
    using Work = std::variant<std::monostate, Fiber::ptr, Callback>;

    struct Task
    {
        Work work;
        ThreadId threadId{ANY_THREAD};

        Task() = default;

        Task(Fiber::ptr fiber, const ThreadId thread_id)
            : work(std::move(fiber)), threadId(thread_id)
        {}

        Task(Callback callback, const ThreadId thread_id)
            : work(std::move(callback)), threadId(thread_id)
        {}

        [[nodiscard]] Fiber::ptr* fiber() { return std::get_if<Fiber::ptr>(&work); }
        [[nodiscard]] Callback* callback() { return std::get_if<Callback>(&work); }

        [[nodiscard]] bool valid() const noexcept
        {
            if (const auto* value = std::get_if<Fiber::ptr>(&work))
            {
                return static_cast<bool>(*value);
            }
            if (const auto* value = std::get_if<Callback>(&work))
            {
                return static_cast<bool>(*value);
            }
            return false;
        }
    };

    struct FetchResult
    {
        Task task;
        bool tickleOthers{false}; // 唤醒其他线程来尝试
        bool retryPending{false}; // 是否需要重试
    };

    [[nodiscard]] static Task MakeTask(Fiber::ptr fiber, const ThreadId thread_id)
    {
        return Task(std::move(fiber), thread_id);
    }

    template<typename Callable>
        requires std::invocable<std::decay_t<Callable> > && std::constructible_from<Callback, Callable>
    [[nodiscard]] static Task MakeTask(Callable&& callback, const ThreadId thread_id)
    {
        return Task(Callback(std::forward<Callable>(callback)), thread_id);
    }

    template<typename FiberOrCallback>
    bool scheduleNoLock(FiberOrCallback&& task, const ThreadId thread_id)
    {
        Task t = MakeTask(std::forward<FiberOrCallback>(task), thread_id);
        if (!t.valid())
        {
            return false;
        }
        const bool need_tickle = m_tasks.empty() || hasIdleThreads();
        m_tasks.emplace_back(std::move(t));
        return need_tickle;
    }

    /// 获取一个适合当前线程的任务
    [[nodiscard]] FetchResult fetchTask(ThreadId current_thread);

    /// 队列上是否有适合当前线程执行的任务
    [[nodiscard]] bool hasRunnableTaskNoLock(ThreadId current_thread) const;

    [[nodiscard]] bool canStopNoLock() const noexcept;

    /// 任务完成收尾工作
    void taskFinished() noexcept;

    /// 报告任务异常
    void reportFiberFailure(const Fiber::ptr& fiber) const noexcept;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<std::thread> m_threads;
    std::list<Task> m_tasks;
    Fiber::ptr m_rootFiber;
    std::string m_name;
    bool m_useCaller{false}; // 是否把主调度协程加入线程池
    bool m_started{false};
    std::size_t m_registerWorkers{0};

protected:
    std::vector<ThreadId> m_threadIds;
    std::size_t m_threadCount{0};
    std::atomic<std::size_t> m_activeThreadCount{0};
    std::atomic<std::size_t> m_idleThreadCount{0};
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_autoStop{false};
    ThreadId m_rootThreadId{ANY_THREAD};
};

class PANS_API SchedulerSwitcher final
{
public:
    explicit SchedulerSwitcher(Scheduler* target = nullptr);
    ~SchedulerSwitcher() noexcept;

    SchedulerSwitcher(const SchedulerSwitcher&) = delete;
    SchedulerSwitcher operator=(const SchedulerSwitcher&) = delete;
    SchedulerSwitcher(SchedulerSwitcher&&) = delete;
    SchedulerSwitcher operator=(SchedulerSwitcher&&) = delete;

private:
    Scheduler* m_caller = nullptr;
};
}

#endif //PANS_INCLUDE_PANS_SCHEDULER_H
