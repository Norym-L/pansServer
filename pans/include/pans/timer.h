#ifndef PANS_INCLUDE_PANS_TIMER_H
#define PANS_INCLUDE_PANS_TIMER_H

#include <chrono>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <pans/export.h>
#include <pans/types.h>

namespace pans
{
class TimerManager;

class PANS_API Timer final : public std::enable_shared_from_this<Timer>
{
    friend class TimerManager;

public:
    using ptr = std::shared_ptr<Timer>;
    using Callback = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    /// 取消
    bool cancel();
    /// 重新计时
    bool refresh();
    /// 重置计时器
    bool reset(Duration interval, bool from_now);
    bool reset(u64 milliseconds, bool from_now);

private:
    struct Comparator;

    /**
     * 构造任务
     * @param interval 时间
     * @param callback 任务
     * @param recurring 是否循环执行
     * @param manager 管理器
     */
    Timer(Duration interval, Callback callback, bool recurring, TimerManager* manager);

private:
    Duration m_interval{Duration::zero()};
    Clock::time_point m_next{}; // 下次执行时间
    Callback m_callback;
    std::atomic<TimerManager*> m_manager{nullptr};
    bool m_recurring{false}; // 是否循环执行
};

class PANS_API TimerManager
{
    friend class Timer;

public:
    using Clock = Timer::Clock;
    using Duration = Timer::Duration;
    using Callback = Timer::Callback;

    TimerManager();
    virtual ~TimerManager();

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;
    TimerManager(TimerManager&&) = delete;
    TimerManager& operator=(TimerManager&&) = delete;

    Timer::ptr addTimer(Duration interval, Callback callback, bool recurring = false);
    Timer::ptr addTimer(u64 milliseconds, Callback callback, bool recurring = false);

    /// 条件执行， condition 条件指针
    Timer::ptr addConditionTimer(Duration interval, Callback callback, std::weak_ptr<void> condition, bool recurring = false);
    Timer::ptr addConditionTimer(u64 milliseconds, Callback callback, std::weak_ptr<void> condition, bool recurring = false);

    /// 获取下一个任务的剩余时间
    [[nodiscard]] std::optional<Duration> getNextTimeout() const;
    [[nodiscard]] u64 getNextTimeoutU64() const;

    /// 获取超时任务
    void listExpiredCallback(std::vector<Callback>& callbacks);

    [[nodiscard]] bool hasTimer() const;

    void cancelAllTimers();
    bool cancel(const Timer::ptr& timer);
    bool refresh(const Timer::ptr& timer);
    bool reset(const Timer::ptr& timer, Duration interval, bool from_now);
    bool reset(const Timer::ptr& timer, u64 milliseconds, bool from_now);

protected:
    virtual void onTimerInsertedAtFront() = 0;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}


#endif //PANS_INCLUDE_PANS_TIMER_H
