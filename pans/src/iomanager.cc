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

void LogEpollError(const char* operation, int fd) noexcept
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

        [[nodiscard]] EventContext& context(Event event)
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
        u32 events{0};
        u32 generation{1};
    };

    [[nodiscard]] FdContext* findContext(int fd) const
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

    std::atomic<bool> m_shuttingDown{false};
    std::mutex m_mutex;
    std::unordered_map<u64, std::function<void()>> m_waitCancellations;
    u64 m_nextWaitRegistration{0};
    int m_epollFd{-1};
    int m_wakeFd{-1};
    std::atomic<std::size_t> m_pendingEventCount{0}; // epoll 上注册的事件
    mutable std::shared_mutex m_contextMutex;        // 保护 epoll
    std::unordered_map<int, std::unique_ptr<FdContext>> m_contexts;
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

} // namespace pans