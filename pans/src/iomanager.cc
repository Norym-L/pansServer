#include "pans/iomanager.h"

#include <algorithm>
#include <cstring>
#include <shared_mutex>
#include <unordered_set>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <pans/iomanager.h>
#include <pans/logger/log.h>
#include <pans/macros.h>

namespace pans
{
namespace
{
const LoggerPtr g_logger = PANS_LOG_NAME("system");
constexpr int MAX_EPOLL_TIMEOUT_MS = 3000;
constexpr std::size_t MAX_EPOLL_EVENTS = 256;
constexpr u64 WAKE_TOKEN = MAX_U64; // 标记事件类型

[[nodiscard]] std::unordered_set<IOManager*>& GetIOManagerRegistry()
{
    static auto* manager = new std::unordered_set<IOManager*>;
    return *manager;
}

[[nodiscard]] std::mutex& GetIOManagerMutex()
{
    static auto* s_mutex = new std::mutex;
    return *s_mutex;
}

void LogEpollError(const char* operation, const int fd) noexcept
{
    const int error = errno;
    LOG_ERROR << operation << " fd=" << fd << " failed: errno=" << error << " (" << std::strerror(error) << ")";
}
}

struct IOManager::Impl
{
    struct EventContext
    {
        using Callback = std::function<void()>;
        using Action = std::variant<std::monostate, Fiber::ptr, Callback>;
        Scheduler* scheduler{nullptr};
        Action action;

        [[nodiscard]] bool valid() const noexcept
        {
            if (scheduler == nullptr)
            {
                return false;
            }
            if (const auto* fiber = std::get_if<Fiber::ptr>(&action))
            {
                return static_cast<bool>(*fiber);
            }
            if (const auto* callback = std::get_if<Callback>(&action))
            {
                return static_cast<bool>(*callback);
            }
            return false;
        }

        void reset() noexcept
        {
            scheduler = nullptr;
            action.emplace<std::monostate>();
        }
    };

    // 断言移动构造和移动赋值构造是否是异常安全的
    static_assert(std::is_nothrow_move_constructible_v<EventContext>);
    static_assert(std::is_nothrow_move_assignable_v<EventContext>);

    struct FdContext
    {
        explicit FdContext(const int descriptor)
            : fd(descriptor)
        {}

        /// 获取 读/写 事件
        [[nodiscard]] EventContext& context(const Event event)
        {
            if (event == Event::READ)
            {
                return read;
            }
            if (event == Event::WRITE)
            {
                return write;
            }
            throw std::invalid_argument("invalid io event");
        }

        /// 升级版本号
        void advanceGeneration() noexcept
        {
            ++generation;
            if (generation == 0U)
            {
                generation = 1;
            }
        }

        std::mutex mutex;
        EventContext read;
        EventContext write;
        s32 fd{-1};
        u32 events{0};     /*注册的事件*/
        u32 generation{1}; /*连接的版本，抛弃不同版本的请求*/
    };

    [[nodiscard]] FdContext* findContext(const int fd) const
    {
        std::shared_lock lock(m_contextMutex);
        const auto it = m_contexts.find(fd);
        return it == m_contexts.end() ? nullptr : it->second.get();
    }

    [[nodiscard]] FdContext* getOrCreateContext(int fd)
    {
        {
            std::shared_lock lock(m_contextMutex);
            const auto it = m_contexts.find(fd);
            if (it != m_contexts.end())
            {
                return it->second.get();
            }
        }
        std::unique_lock lock(m_contextMutex);
        auto [it, inserted] = m_contexts.try_emplace(fd);
        if (inserted)
        {
            it->second = std::make_unique<FdContext>(fd);
        }
        return it->second.get();
    }

    /**
     * 更新事件
     * @param context fd ctx
     * @param new_events 新的事件 0 删除fd
     * @return 是否成功
     */
    [[nodiscard]] bool updateInterest(FdContext& context, const u32 new_events) const
    {
        const int operation = context.events == 0U ? EPOLL_CTL_ADD : (new_events == 0U ? EPOLL_CTL_DEL : EPOLL_CTL_MOD);
        epoll_event event{};
        event.events = EPOLLET;

        // 可以写成 event.events |= new_events, 因为定义的 Event 枚举和 EPOLL 枚举是对应的
        if (new_events & static_cast<u32>(Event::READ))
        {
            event.events |= EPOLLIN;
        }
        if (new_events & static_cast<u32>(Event::WRITE))
        {
            event.events |= EPOLLOUT;
        }
        // 存储 generation + fd
        event.data.u64 = (static_cast<u64>(context.generation) << 32U) | static_cast<u32>(context.fd);
        if (::epoll_ctl(m_epollFd, operation, context.fd, &event) != 0)
        {
            // fd 不存在，且为删除操作 视为成功
            if (new_events == 0U && (errno == EBADF || errno == ENOENT))
            {
                context.advanceGeneration();
                context.events = 0U;
                return true;
            }
            LogEpollError("epoll_ctl", context.fd);
            return false;
        }
        if (new_events == 0U) // 删除操作
        {
            context.advanceGeneration();
        }
        context.events = new_events;
        return true;
    }

    /// 获取 fd 上的事件
    [[nodiscard]] static EventContext TakeEvent(FdContext& context, const Event event)
    {
        EventContext result = std::move(context.context(event));
        context.context(event).reset();
        return result;
    }

    /// 把 EventContext 中的操作挂载到任务队列
    static void Dispatch(EventContext event) noexcept
    {
        if (!event.valid())
        {
            return;
        }
        try
        {
            if (auto* callback = std::get_if<EventContext::Callback>(&event.action))
            {
                event.scheduler->addTask(std::move(*callback));
            }
            else if (auto* fiber = std::get_if<Fiber::ptr>(&event.action))
            {
                if ((*fiber)->getState() == Fiber::State::WAITING)
                {
                    (void) (*fiber)->wake();
                }
                event.scheduler->addTask(std::move(*fiber));
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "could not dispatch IO event: " << e.what();
        }
        catch (...)
        {
            LOG_ERROR << "could not dispatch IO event: unknown error";
        }
    }

    static_assert(static_cast<u32>(Event::READ) == static_cast<u32>(EPOLLIN));
    static_assert(static_cast<u32>(Event::WRITE) == static_cast<u32>(EPOLLOUT));

    void processReadyEvent(const epoll_event& event)
    {
        const u64 token = event.data.u64;
        if (token == WAKE_TOKEN)
        {
            u64 value{0};
            while (::read(m_wakeFd, &value, sizeof(value)) < 0 && errno == EINTR)
            {}
            return;
        }
        FdContext* context = findContext(static_cast<int>(static_cast<u32>(token))); // fd 等于 token 的低32位
        if (!context)
        {
            return;
        }
        EventContext read_context;
        EventContext write_context;
        std::size_t triggered{0};
        {
            std::lock_guard lock(context->mutex);
            if (context->generation != static_cast<u32>(token >> 32) || context->events == 0U) // 旧事件
            {
                return;
            }
            u32 ready_bits = event.events;
            if (ready_bits & (EPOLLERR | EPOLLHUP)) // 网络错误会上报 EPOLLERR 或是 EPOLLHUP
            {
                ready_bits |= EPOLLET | ((EPOLLIN | EPOLLOUT) & context->events); // 将错误伪装成应用层关心的 读/写 事件，触发应用层的读写socket，让应用层发现错误
            }
            const u32 ready = ready_bits & (EPOLLIN | EPOLLOUT) & context->events; // 触发的事件
            if (ready == 0U)
            {
                return;
            }

            const u32 registered = context->events;    // 注册的事件
            const u32 remaining = registered & ~ready; // 删掉触发的事件
            u32 dispatch_events = ready;
            if (!updateInterest(*context, remaining))
            {
                dispatch_events = registered; // 更新失败就全触发
                context->events = 0U;
                context->advanceGeneration();
            }
            if (dispatch_events & static_cast<u32>(Event::READ))
            {
                read_context = TakeEvent(*context, Event::READ);
                ++triggered;
            }
            if (dispatch_events & static_cast<u32>(Event::WRITE))
            {
                write_context = TakeEvent(*context, Event::WRITE);
                ++triggered;
            }
        }
        m_pendingEventCount.fetch_sub(triggered, std::memory_order_acq_rel);
        Dispatch(std::move(read_context));
        Dispatch(std::move(write_context));
    }

    std::atomic<bool> m_shuttingDown{false};
    std::mutex m_mutex;
    std::unordered_map<u64, std::function<void()> > m_waitCancellations;
    u64 m_nextWaitRegistration{0};
    int m_epollFd{-1};
    int m_wakeFd{-1};
    std::atomic<std::size_t> m_pendingEventCount{0}; // epoll 上注册的事件
    mutable std::shared_mutex m_contextMutex;        // 保护 epoll
    std::unordered_map<int, std::unique_ptr<FdContext> > m_contexts;
};

IOManager::IOManager(const std::size_t thread, const bool use_caller, const std::string& name)
    : Scheduler(thread, use_caller, name), m_impl(std::make_unique<Impl>())
{
    m_impl->m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_impl->m_epollFd < 0)
    {
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }
    m_impl->m_wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC | EFD_SEMAPHORE);
    if (m_impl->m_wakeFd < 0)
    {
        const int error = errno;
        ::close(m_impl->m_epollFd);
        m_impl->m_epollFd = -1;
        throw std::system_error(error, std::generic_category(), "eventfd");
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = WAKE_TOKEN;
    if (::epoll_ctl(m_impl->m_epollFd, EPOLL_CTL_ADD, m_impl->m_wakeFd, &event) != 0)
    {
        const int error = errno;
        ::close(m_impl->m_epollFd);
        ::close(m_impl->m_wakeFd);
        m_impl->m_epollFd = -1;
        m_impl->m_wakeFd = -1;
        throw std::system_error(error, std::generic_category(), "epoll_ctl eventfd");
    }
    try
    {
        Scheduler::start();
        std::lock_guard registry_lock(GetIOManagerMutex());
        GetIOManagerRegistry().insert(this);
    }
    catch (...)
    {
        try
        {
            Scheduler::stop();
        }
        catch (...)
        {}
        ::close(m_impl->m_wakeFd);
        ::close(m_impl->m_epollFd);
        m_impl->m_wakeFd = -1;
        m_impl->m_epollFd = -1;
        throw;
    }
}

int IOManager::addEvent(const int fd, Event event, std::function<void()> callback)
{
    ASSERT_RETVAL(fd > 0 && (event == Event::READ || event == Event::WRITE), -1);
    std::lock_guard lock(m_impl->m_mutex);
    if (m_impl->m_shuttingDown.load(std::memory_order_acquire))
    {
        errno = ECANCELED;
        return -1;
    }
    Impl::EventContext new_context;
    new_context.scheduler = Scheduler::GetThis(); // 在发起的调度器上执行操作
    if (callback)
    {
        new_context.action.emplace<Impl::EventContext::Callback>(std::move(callback));
        if (new_context.scheduler == nullptr)
        {
            new_context.scheduler = this;
        }
    }
    else
    {
        auto fiber = Fiber::GetThis();
        ASSERT_RETVAL(new_context.scheduler && fiber && fiber->getState() == Fiber::State::EXEC, -1);
        new_context.action.emplace<Fiber::ptr>(std::move(fiber));
    }
    Impl::FdContext* context = m_impl->getOrCreateContext(fd);
    std::lock_guard ctx_lock(context->mutex);
    if (context->events & static_cast<u32>(event)) // 存在重复添加
    {
        LOG_ERROR << "addEvent fd=" << fd << " event=" << static_cast<u32>(context->events);
        ASSERT_RETVAL(false, -1);
    }
    const u32 new_events = context->events | static_cast<u32>(event);
    if (!m_impl->updateInterest(*context, new_events))
    {
        ASSERT_RETVAL(false, -2);
    }
    context->context(event) = std::move(new_context);
    m_impl->m_pendingEventCount.fetch_add(1, std::memory_order_acq_rel);
    return 0;
}

bool IOManager::delEvent(const int fd, const Event event)
{
    auto* context = m_impl->findContext(fd);
    if (!context)
    {
        return false;
    }
    [[maybe_unused]] Impl::EventContext discarded; // 析构放到外面
    {
        std::lock_guard lock(context->mutex);
        const u32 event_mask = static_cast<u32>(event);
        if (!(context->events & event_mask)) // 删除不存在的事件
        {
            return false;
        }
        if (!m_impl->updateInterest(*context, context->events & ~event_mask))
        {
            return false;
        }
        discarded = Impl::TakeEvent(*context, event);
    }
    m_impl->m_pendingEventCount.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

bool IOManager::cancelEvent(const int fd, const Event event)
{
    auto* context = m_impl->findContext(fd);
    if (!context)
    {
        return false;
    }
    Impl::EventContext canceled;
    {
        std::lock_guard lock(context->mutex);
        const u32 event_mask = static_cast<u32>(event);
        if (!(context->events & event_mask))
        {
            return false;
        }
        if (!m_impl->updateInterest(*context, context->events & ~event_mask))
        {
            return false;
        }
        canceled = Impl::TakeEvent(*context, event);
    }
    m_impl->m_pendingEventCount.fetch_sub(1, std::memory_order_acq_rel);
    Impl::Dispatch(std::move(canceled));
    return true;
}

bool IOManager::cancelAll(const int fd)
{
    auto* context = m_impl->findContext(fd);
    if (!context)
    {
        return false;
    }
    Impl::EventContext read_context;
    Impl::EventContext write_context;
    std::size_t canceled = 0; // 标记实际取消的事件数量
    {
        std::lock_guard lock(context->mutex);
        if (context->events == 0U)
        {
            return false;
        }
        const u32 old_events = context->events;
        if (!m_impl->updateInterest(*context, 0))
        {
            return false;
        }
        if (old_events & static_cast<u32>(Event::READ)) // 存在读事件
        {
            read_context = Impl::TakeEvent(*context, Event::READ);
            ++canceled;
        }
        if (old_events & static_cast<u32>(Event::WRITE))
        {
            write_context = Impl::TakeEvent(*context, Event::WRITE);
            ++canceled;
        }
    }
    m_impl->m_pendingEventCount.fetch_sub(canceled, std::memory_order_acq_rel);
    Impl::Dispatch(std::move(read_context));
    Impl::Dispatch(std::move(write_context));
    return true;
}

bool IOManager::CancelFdAcrossManagers(const int fd)
{
    bool canceled = false;
    std::lock_guard registry_lock(GetIOManagerMutex());
    for (auto* mamager: GetIOManagerRegistry())
    {
        canceled = mamager->cancelAll(fd) || canceled;
    }
    return canceled;
}

bool IOManager::CancelEventAcrossManagers(const int fd, const Event event)
{
    bool canceled = false;
    std::lock_guard registry_lock(GetIOManagerMutex());
    for (auto* mamager: GetIOManagerRegistry())
    {
        canceled = mamager->cancelEvent(fd, event) || canceled;
    }
    return canceled;
}

IOManager* IOManager::GetThis() noexcept
{
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

u64 IOManager::registerCancellationAction(std::function<void()> action)
{
    ASSERT_RETVAL2(action, INVALID64, "wait cancellation action must not be empty");
    std::lock_guard lock(m_impl->m_mutex);
    if (m_impl->m_shuttingDown.load(std::memory_order_acquire))
    {
        return 0;
    }
    const u64 key = ++m_impl->m_nextWaitRegistration;
    m_impl->m_waitCancellations.emplace(key, std::move(action));
    return key;
}

void IOManager::unregisterCancellationAction(const u64 key) noexcept
{
    [[maybe_unused]] std::function<void()> discarded;
    {
        std::lock_guard lock(m_impl->m_mutex);
        const auto it = m_impl->m_waitCancellations.find(key);
        if (it == m_impl->m_waitCancellations.end())
        {
            return;
        }
        discarded = std::move(it->second);
        m_impl->m_waitCancellations.erase(it);
    }
}

bool IOManager::isShuttingDown() const noexcept
{
    return m_impl->m_shuttingDown.load(std::memory_order_acquire);
}

void IOManager::tickle()
{
    constexpr u64 ONE = 1;
    ssize_t result{0};
    do
    {
        result = ::write(m_impl->m_wakeFd, &ONE, sizeof(ONE));
    }
    while (result < 0 && errno == EINTR);
    if (result < 0 && errno != EAGAIN)
    {
        LogEpollError("eventfd write", m_impl->m_wakeFd);
    }
}

bool IOManager::canStop()
{
    u64 timeout = INVALID64;
    return canStop(timeout);
}

bool IOManager::canStop(u64& timeout)
{
    timeout = getNextTimeoutU64();
    return timeout == INVALID64
           && m_impl->m_pendingEventCount.load(std::memory_order_acquire) == 0
           && Scheduler::canStop();
}

void IOManager::idle()
{
    std::array<epoll_event, MAX_EPOLL_EVENTS> events{};
    while (true)
    {
        int ready_count = 0;
        do
        {
            u64 next_timeout = INVALID64;
            if (canStop(next_timeout))
            {
                return;
            }
            const int timeout = next_timeout == INVALID64
                ? MAX_EPOLL_TIMEOUT_MS
                : static_cast<int>(std::min(next_timeout, static_cast<u64>(MAX_EPOLL_TIMEOUT_MS)));
            ready_count = ::epoll_wait(m_impl->m_epollFd, events.data(), static_cast<int>(events.size()), timeout);
        }
        while (ready_count < 0 && errno == EINTR);

        std::vector<TimerManager::Callback> callbacks;
        listExpiredCallback(callbacks);
        if (!callbacks.empty())
        {
            addTask(std::make_move_iterator(callbacks.begin()), std::make_move_iterator(callbacks.end()));
        }
        for (int index = 0; index < ready_count; ++index)
        {
            m_impl->processReadyEvent(events[index]);
        }
        Fiber::YieldToHold(Fiber::WaitReason::External);
    }
}

void IOManager::onTimerInsertedAtFront()
{
    tickle();
}
} // namespace pans
