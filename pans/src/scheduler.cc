#include <algorithm>
#include <charconv>
#include <exception>
#include <ostream>
#include <string>
#include <utility>

#include <pans/logger/log.h>
#include <pans/utils/thread_uitls.h>
#include <pans/scheduler.h>

namespace pans
{
namespace
{
const LoggerPtr g_logger = PANS_LOG_NAME("system");
thread_local Scheduler* t_scheduler = nullptr;
thread_local Fiber* t_schedulerFiber = nullptr;

[[nodiscard]] std::string WorkerName(const std::string& scheduler_name, std::size_t index)
{
    const std::string base = scheduler_name.empty() ? "scheduler" : scheduler_name;
    return base + "_" + std::to_string(index);
}
}

Scheduler::Scheduler(std::size_t threads, const bool use_caller, const std::string& name)
    : m_name(name), m_useCaller(use_caller)
{
    if (threads <= 0)
    {
        throw std::invalid_argument("scheduler requires at least one thread");
    }

    if (m_useCaller)
    {
        if (t_scheduler != nullptr)
        {
            throw std::logic_error("the current thread already owns a caller scheduler"); // 一个线程（主线程）只能做一个调度器的工作线程
        }
        --threads;
        m_rootThreadId = GetThreadId();
        m_threadIds.push_back(m_rootThreadId);
        m_rootFiber = std::make_shared<Fiber>([this]() { run(); });
        t_scheduler = this;
        t_schedulerFiber = m_rootFiber.get();
        if (!m_name.empty())
        {
            SetThreadName(m_name);
        }
    }

    m_threadCount = threads;
}

Scheduler::~Scheduler()
{
    try
    {
        stop();
    }
    catch (...)
    {
        std::terminate();
    }
    if (t_scheduler == this)
    {
        t_scheduler = nullptr;
        t_schedulerFiber = nullptr;
    }
}

Scheduler* Scheduler::GetThis() noexcept
{
    return t_scheduler;
}

Fiber* Scheduler::GetMainFiber() noexcept
{
    return t_schedulerFiber;
}

void Scheduler::start()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_started)
    {
        return;
    }
    if (m_rootFiber)
    {
        const auto state = m_rootFiber->getState();
        if (state == Fiber::State::TERM || state == Fiber::State::EXCEPT)
        {
            m_rootFiber->reset([this]() { run(); });
        }
        else if (state != Fiber::State::INIT)
        {
            throw std::logic_error("caller scheduler root fiber is still active");
        }
    }
    m_autoStop.store(false, std::memory_order_release);
    m_stopping.store(false, std::memory_order_release);
    m_registerWorkers = 0;
    m_started = true;
    m_threads.clear();
    m_threads.reserve(m_threadCount);
    try
    {
        for (std::size_t index = 0; index < m_threadCount; ++index)
        {
            m_threads.emplace_back([this, index]()
                {
                    SetThreadName(WorkerName(m_name, index));
                    run();
                });
        }
    }
    catch (...)
    {
        m_tasks.clear();
        m_autoStop.store(true, std::memory_order_release);
        m_stopping.store(true, std::memory_order_release);
        std::vector<std::thread> threads;
        threads.swap(m_threads);
        m_started = false;
        lock.unlock();
        m_condition.notify_all();
        for (auto& t: threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        throw;
    }
    m_condition.wait(lock, [this]()
                         {
                             return m_registerWorkers == m_threadCount;
                         });
}

Scheduler::FetchResult Scheduler::fetchTask(const ThreadId current_thread)
{
    FetchResult result;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_tasks.begin(); it != m_tasks.end();)
    {
        if (!it->valid())
        {
            it = m_tasks.erase(it);
            continue;
        }
        if (it->threadId != ANY_THREAD && it->threadId != current_thread)
        {
            result.tickleOthers = true;
            ++it;
            continue;
        }
        if (const Fiber::ptr* scheduled_fiber = it->fiber())
        {
            Fiber::State state = (*scheduled_fiber)->getState();
            if (state == Fiber::State::TERM || state == Fiber::State::EXCEPT)
            {
                it = m_tasks.erase(it);
                continue;
            }
            if (state == Fiber::State::EXEC)
            {
                result.retryPending = true;
                ++it;
                continue;
            }
            if (state == Fiber::State::WAITING)
            {
                if (!(*scheduled_fiber)->wake())
                {
                    result.retryPending = true;
                    ++it;
                    continue;
                }
                state = (*scheduled_fiber)->getState();
            }
            if (state != Fiber::State::INIT && state != Fiber::State::READY)
            {
                ++it;
                continue;
            }
        }

        result.task = std::move(*it);
        m_tasks.erase(it);
        m_activeThreadCount.fetch_add(1, std::memory_order_acq_rel);
        break;
    }
    return result;
}

void Scheduler::setThis() noexcept
{
    t_scheduler = this;
}

void Scheduler::tickle()
{
    m_condition.notify_all();
}

void Scheduler::run()
{
    Scheduler* const previous_scheduler = t_scheduler;
    Fiber* const previous_scheduler_fiber = t_schedulerFiber;
    setThis();
    t_schedulerFiber = Fiber::Current();
    const ThreadId thread_id = GetThreadId();
    if (thread_id != m_rootThreadId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_threadIds.push_back(thread_id);
        ++m_registerWorkers;
        m_condition.notify_all();
    }
    const auto idle_fiber = std::make_shared<Fiber>([this]() { idle(); });
    Fiber::ptr callback_fiber;
    while (true)
    {
        auto [task, tickleOthers, retryPending] = fetchTask(thread_id);
        if (tickleOthers)
        {
            tickle();
        }
        if (!task.valid())
        {
            if (canStop())
            {
                break;
            }
            if (retryPending)
            {
                std::this_thread::yield();
                continue;
            }
            m_idleThreadCount.fetch_add(1, std::memory_order_acq_rel);
            try
            {
                Fiber::State idle_state = idle_fiber->getState();
                if (idle_state == Fiber::State::WAITING)
                {
                    (void) idle_fiber->wake();
                    idle_state = idle_fiber->getState();
                }
                if (idle_state == Fiber::State::INIT || idle_state == Fiber::State::READY)
                {
                    idle_fiber->resume();
                }
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR << "scheduler=" << m_name << " idle fier failed: " << exception.what();
            }
            m_idleThreadCount.fetch_sub(1, std::memory_order_acq_rel);
            if (idle_fiber->getState() == Fiber::State::EXCEPT)
            {
                reportFiberFailure(idle_fiber);
                break;
            }
            if (idle_fiber->getState() == Fiber::State::TERM) // 调度器需要停止
            {
                break;
            }
            continue;
        }
        Fiber::ptr running_fiber;
        const bool callback_task = task.callback() != nullptr;
        if (Fiber::ptr* fiber = task.fiber())
        {
            running_fiber = std::move(*fiber);
        }
        else if (Callback* callback = task.callback())
        {
            if (callback_fiber && (callback_fiber->getState() == Fiber::State::TERM || callback_fiber->getState() == Fiber::State::EXCEPT))
            {
                callback_fiber->reset(std::move(*callback));
            }
            else
            {
                callback_fiber = std::make_shared<Fiber>(std::move(*callback));
            }
            running_fiber = callback_fiber;
        }
        Fiber::State suspend_state = Fiber::State::EXCEPT;
        bool resumed = false;
        try
        {
            suspend_state = running_fiber->resume();
            resumed = true;
        }
        catch (const std::exception& exception)
        {
            LOG_ERROR << "scheduler=" << m_name << " could not resume fiber=" << running_fiber->getId() << ": " << exception.what();
        }
        catch (...)
        {
            LOG_ERROR << "scheduler=" << m_name << " could not resume fiber=" << running_fiber->getId() << ": unknown error";
        }
        const Fiber::State state = resumed ? suspend_state : running_fiber->getState();
        if (state == Fiber::State::READY)
        {
            addTask(running_fiber);
            if (callback_task)
            {
                callback_fiber.reset();
            }
        }
        else if (state == Fiber::State::WAITING)
        {
            if (callback_task)
            {
                callback_fiber.reset();
            }
        }
        else if (state == Fiber::State::EXCEPT)
        {
            reportFiberFailure(running_fiber);
        }
        taskFinished();
    }
    t_scheduler = previous_scheduler;
    t_schedulerFiber = previous_scheduler_fiber;
}

void Scheduler::taskFinished() noexcept
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeThreadCount.fetch_sub(1, std::memory_order_acq_rel);
    }
    m_condition.notify_all();
}

bool Scheduler::hasRunnableTaskNoLock(const ThreadId current_thread) const
{
    for (const auto& task: m_tasks)
    {
        if (task.valid() && (task.threadId == ANY_THREAD || task.threadId == current_thread))
        {
            return true;
        }
    }
    return false;
}

void Scheduler::idle()
{
    const ThreadId thread_id = GetThreadId();
    while (!canStop())
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this, thread_id]()
                             {
                                 return hasRunnableTaskNoLock(thread_id) || canStopNoLock();
                             });
        lock.unlock();
        if (!canStop())
        {
            Fiber::YieldToHold(Fiber::WaitReason::External);
        }
    }
}

void Scheduler::stop()
{
    if (GetThis() == this && (!m_useCaller || GetThreadId() != m_rootThreadId)) //  worker 线程不能停止
    {
        throw std::logic_error("Scheduler::stop cannot join its current worker therad");
    }
    if (m_useCaller && GetThreadId() != m_rootThreadId)
    {
        throw std::logic_error("a caller scheduler must be stopped by its construction thread");
    }
    std::size_t wake_count{0};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_autoStop.store(true, std::memory_order_release);
        m_stopping.store(true, std::memory_order_release);
        if (!m_started && m_threads.empty() && m_tasks.empty())
        {
            return;
        }
        wake_count = m_threadCount + (m_rootFiber ? 1 : 0);
    }
    m_condition.notify_all();
    for (std::size_t index = 0; index < wake_count; ++index)
    {
        tickle();
    }
    if (m_rootFiber && !canStop())
    {
        auto state = m_rootFiber->getState();
        if (state == Fiber::State::WAITING)
        {
            (void) m_rootFiber->wake();
            state = m_rootFiber->getState();
        }
        if (state == Fiber::State::INIT || state == Fiber::State::READY)
        {
            m_rootFiber->resume();
        }
        if (m_rootFiber->getState() == Fiber::State::EXCEPT)
        {
            reportFiberFailure(m_rootFiber);
        }
    }
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        threads.swap(m_threads);
    }
    for (auto& t: threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_started = false;
        m_registerWorkers = 0;
        if (m_useCaller)
        {
            m_threadIds.erase(std::ranges::remove_if(
                m_threadIds,
                [this](const ThreadId id)
                    {
                        return id != m_rootThreadId;
                    }).begin());
        }
        else
        {
            m_threadIds.clear();
        }
    }
}

bool Scheduler::canStopNoLock() const noexcept
{
    return m_autoStop.load(std::memory_order_acquire) &&
           m_stopping.load(std::memory_order_acquire) &&
           m_tasks.empty() &&
           m_activeThreadCount.load(std::memory_order_acquire) == 0;
}

void Scheduler::reportFiberFailure(const Fiber::ptr& fiber) const noexcept
{
    try
    {
        fiber->rethrowIfFailed();
    }
    catch (const std::exception& exception)
    {
        LOG_ERROR << "scheduler=" << m_name << " fiber=" << fiber->getId() << " feiled: " << exception.what();
    }
    catch (...)
    {
        LOG_ERROR << "scheduler=" << m_name << " fiber=" << fiber->getId() << " feiled with a unknown excetion";
    }
}

bool Scheduler::canStop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return canStopNoLock();
}

void Scheduler::switchTo(const ThreadId thread_id)
{
    const Scheduler* const source = GetThis();
    if (source == this && (thread_id == ANY_THREAD || thread_id == GetThreadId()))
    {
        return;
    }
    Fiber::ptr current = Fiber::GetThis();
    if (!current)
    {
        throw std::logic_error("Scheduler::switchTo must be called inside a Fiber");
    }
    addTask(current, thread_id);
    Fiber::YieldToHold(Fiber::WaitReason::External);
}

std::ostream& Scheduler::dump(std::ostream& output) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    output << "[Scheduler name=" << m_name
        << " workers=" << m_threadCount
        << " active=" << m_activeThreadCount.load(std::memory_order_acquire)
        << " idle=" << m_idleThreadCount.load(std::memory_order_acquire)
        << " stopping=" << m_stopping.load(std::memory_order_acquire)
        << "]\n\t";
    for (std::size_t index = 0; index < m_threadIds.size(); ++index)
    {
        if (index != 0)
        {
            output << ", ";
        }
        output << m_threadIds[index];
    }
    return output;
}

SchedulerSwitcher::SchedulerSwitcher(Scheduler* target)
    : m_caller(Scheduler::GetThis())
{
    if (target != nullptr)
    {
        target->switchTo();
    }
}

SchedulerSwitcher::~SchedulerSwitcher() noexcept
{
    if (m_caller == nullptr)
    {
        return;
    }
    try
    {
        m_caller->switchTo();
    }
    catch (const std::exception& exception)
    {
        LOG_ERROR << "SchedulerSwitcher could not return to the caller: " << exception.what();
    }
    catch (...)
    {
        LOG_ERROR << "SchedulerSwitcher could not return to the caller with unknow error";
    }
}
}
