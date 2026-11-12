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
        u32 events{0};     /*事件标记*/
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

// bool IOManager::CancelFdAcrossManagers(int fd) {}
// bool IOManager::CancelEventAcrossManagers(int fd, Event event) {}
// IOManager* IOManager::GetThis() noexcept {}
// u64 IOManager::registerCancellationAction(std::function<void()> action) {}
// void IOManager::unregisterCancellationAction(u64 key) noexcept {}
// bool IOManager::isShuttingDown() const noexcept {}
// void IOManager::tickle() {}
// bool IOManager::canStop() {}
// void IOManager::idle() {}
// void IOManager::onTimerInsertedAtFront() {}
// bool IOManager::canStop(u64& timeout) {}
} // namespace pans
