#include <algorithm>
#include <iterator>
#include <limits>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <stdexcept>

#include <pans/timer.h>
#include <pans/macros.h>

namespace pans
{
namespace
{
[[nodiscard]] Timer::Duration CheckedDuration(const u64 miliseconds)
{
    using Rep = Timer::Duration::rep;
    constexpr auto MAX = static_cast<u64>(std::numeric_limits<Rep>::max());
    if (miliseconds > MAX)
    {
        throw std::overflow_error("timer interval exceeds milliseconds range");
    }
    return Timer::Duration(static_cast<Rep>(miliseconds));
}
}

struct Timer::Comparator
{
    bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const noexcept
    {
        if (lhs == rhs)
        {
            return false;
        }
        if (!lhs)
        {
            return true;
        }
        if (!rhs)
        {
            return false;
        }
        if (lhs->m_next != rhs->m_next)
        {
            return lhs->m_next < rhs->m_next;
        }
        return lhs.get() < rhs.get();
    }
};

struct TimerManager::Impl
{
    mutable std::shared_mutex mutex;
    std::set<Timer::ptr, Timer::Comparator> timers;
};

Timer::Timer(const Duration interval, Callback callback, const bool recurring, TimerManager* manager)
    : m_interval(interval), m_next(Clock::now() + interval), m_callback(std::move(callback)), m_manager(manager), m_recurring(recurring)
{}

bool Timer::cancel()
{
    auto* manager = m_manager.load();
    return manager != nullptr && manager->cancel(shared_from_this());
}

bool Timer::refresh()
{
    auto* manager = m_manager.load();
    return manager != nullptr && manager->refresh(shared_from_this());
}

bool Timer::reset(const Duration interval, const bool from_now)
{
    auto* manager = m_manager.load();
    return manager != nullptr && manager->reset(shared_from_this(), interval, from_now);
}

bool Timer::reset(const u64 milliseconds, const bool from_now)
{
    return reset(CheckedDuration(milliseconds), from_now);
}

TimerManager::TimerManager()
    : m_impl(std::make_unique<TimerManager::Impl>())
{}

TimerManager::~TimerManager()
{
    cancelAllTimers();
}

Timer::ptr TimerManager::addTimer(const Duration interval, Callback callback, const bool recurring)
{
    // callback存在且不死循环
    ASSERT_RETVAL2(callback && (interval > Duration::zero() || (interval == Duration::zero() && !recurring)), nullptr, "invalid parms when adding timmer");
    Timer::ptr timer(new Timer(interval, std::move(callback), recurring, this));
    bool inserted_at_front = false;
    {
        std::unique_lock lock(m_impl->mutex);
        const auto inserted_it = m_impl->timers.insert(timer).first;
        inserted_at_front = inserted_it == m_impl->timers.begin();
    }
    if (inserted_at_front)
    {
        onTimerInsertedAtFront();
    }
    return timer;
}

Timer::ptr TimerManager::addTimer(const u64 milliseconds, Callback callback, const bool recurring)
{
    return addTimer(CheckedDuration(milliseconds), std::move(callback), recurring);
}

Timer::ptr TimerManager::addConditionTimer(const Duration interval, Callback callback, std::weak_ptr<void> condition, const bool recurring)
{
    ASSERT_RETVAL2(callback, nullptr, "condition timer callback must not be empty");
    return addTimer(interval,
                    [condition = std::move(condition), callback = std::move(callback)]() mutable
                        {
                            if (condition.lock())
                            {
                                callback();
                            }
                        },
                    recurring);
}

Timer::ptr TimerManager::addConditionTimer(const u64 milliseconds, Callback callback, std::weak_ptr<void> condition, const bool recurring)
{
    return addConditionTimer(CheckedDuration(milliseconds), std::move(callback), std::move(condition), recurring);
}

std::optional<TimerManager::Duration> TimerManager::getNextTimeout() const
{
    std::shared_lock lock(m_impl->mutex);
    if (m_impl->timers.empty())
    {
        return std::nullopt;
    }
    const Clock::time_point now = Clock::now();
    const Clock::time_point next = (*m_impl->timers.begin())->m_next;
    if (next <= now)
    {
        return Duration::zero();
    }
    return std::chrono::ceil<Duration>(next - now);
}

u64 TimerManager::getNextTimeoutU64() const
{
    const auto timeout = getNextTimeout();
    if (!timeout)
    {
        return INVALID64;
    }
    return static_cast<u64>(timeout->count());
}

void TimerManager::listExpiredCallback(std::vector<Callback>& callbacks)
{
    std::vector<Callback> expired_callbacks;
    const Clock::time_point now = Clock::now();
    {
        std::unique_lock lock(m_impl->mutex);
        auto end = m_impl->timers.begin();
        while (end != m_impl->timers.end() && (*end)->m_next <= now)
        {
            ++end;
        }
        if (end == m_impl->timers.begin())
        {
            return;
        }

        std::vector<Timer::ptr> expired;
        expired.reserve(std::distance(m_impl->timers.begin(), end));
        expired.assign(m_impl->timers.begin(), end);

        m_impl->timers.erase(m_impl->timers.begin(), end);
        expired_callbacks.reserve(expired.size());
        for (auto& timer: expired)
        {
            if (timer->m_recurring) // 需要重复执行
            {
                expired_callbacks.push_back(timer->m_callback);
                timer->m_next = now + timer->m_interval;
                m_impl->timers.insert(std::move(timer));
            }
            else
            {
                expired_callbacks.push_back(std::move(timer->m_callback));
                timer->m_manager = nullptr;
            }
        }
    }
    callbacks.insert(callbacks.end(),
                     std::make_move_iterator(expired_callbacks.begin()),
                     std::make_move_iterator(expired_callbacks.end()));
}

bool TimerManager::hasTimer() const
{
    std::shared_lock lock(m_impl->mutex);
    return !m_impl->timers.empty();
}

void TimerManager::cancelAllTimers()
{
    std::vector<Callback> discarded_callbacks;
    {
        std::unique_lock lock(m_impl->mutex);
        discarded_callbacks.reserve(m_impl->timers.size());
        for (auto& timer: m_impl->timers)
        {
            timer->m_manager = nullptr;
            if (timer->m_callback)
            {
                discarded_callbacks.push_back(std::move(timer->m_callback)); // 在锁外析构 callback，防止存在析构 TimerManager 导致死锁
            }
        }
        m_impl->timers.clear();
    }
}

bool TimerManager::cancel(const Timer::ptr& timer)
{
    [[maybe_unused]] Callback discarded_callback;
    {
        std::unique_lock lock(m_impl->mutex);
        if (!timer || timer->m_manager != this || !timer->m_callback)
        {
            return false;
        }
        const auto it = m_impl->timers.find(timer);
        if (it == m_impl->timers.end())
        {
            return false;
        }
        m_impl->timers.erase(it);
        timer->m_manager = nullptr;
        // ReSharper disable once CppJoinDeclarationAndAssignment 在锁外析构 callback，防止存在析构 TimerManager 导致死锁
        discarded_callback = std::move(timer->m_callback);
    }
    return true;

    (void) discarded_callback; // 抑制警告用
}

bool TimerManager::refresh(const Timer::ptr& timer)
{
    std::unique_lock lock(m_impl->mutex);
    if (!timer || timer->m_manager != this || !timer->m_callback)
    {
        return false;
    }
    const auto it = m_impl->timers.find(timer);
    if (it == m_impl->timers.end())
    {
        return false;
    }
    m_impl->timers.erase(it);
    timer->m_next = Clock::now() + timer->m_interval;
    m_impl->timers.insert(timer);
    return true;
}

bool TimerManager::reset(const Timer::ptr& timer, const Duration interval, const bool from_now)
{
    ASSERT_RETVAL(timer && (interval > Duration::zero() || (!timer->m_recurring && interval == Duration::zero())), false);
    bool inserted_at_front = false;
    {
        std::unique_lock lock(m_impl->mutex);
        if (!timer || timer->m_manager != this || !timer->m_callback)
        {
            return false;
        }
        const auto it = m_impl->timers.find(timer);
        if (it == m_impl->timers.end())
        {
            return false;
        }
        const auto start = from_now ? Clock::now() : timer->m_next - timer->m_interval;
        m_impl->timers.erase(it);
        timer->m_interval = interval;
        timer->m_next = start + interval;
        const auto inserted_it = m_impl->timers.insert(timer).first;
        inserted_at_front = inserted_it == m_impl->timers.begin();
    }
    if (inserted_at_front)
    {
        onTimerInsertedAtFront();
    }
    return true;
}

bool TimerManager::reset(const Timer::ptr& timer, const u64 milliseconds, const bool from_now)
{
    return reset(timer, CheckedDuration(milliseconds), from_now);
}
}
