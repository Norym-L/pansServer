#include <fcntl.h>
#include <mutex>
#include <sys/socket.h>
#include <sys/stat.h>
#include <ranges>

#include <pans/fd_manager.h>

namespace pans
{
FdCtx::FdCtx(const int fd)
    : m_fd(fd)
{
    initialize();
}

void FdCtx::initialize() noexcept
{
    struct stat states{};
    if (m_initialized || m_fd < 0 || ::fstat(m_fd, &states) != 0)
    {
        m_initialized = false;
        return;
    }
    m_initialized = true;
    m_socket = S_ISSOCK(states.st_mode);
    if (!m_socket)
    {
        return;
    }
    const int flags = fcntl(m_fd, F_GETFL, 0);
    if (flags < 0)
    {
        m_initialized = false;
        m_socket = false;
        return;
    }
    const bool nonblocking = (flags & O_NONBLOCK) != 0;
    m_userNonBlock.store(nonblocking, std::memory_order_relaxed);
    if (!nonblocking && fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) != 0)
    {
        m_initialized = false;
        m_socket = false;
        return;
    }
    m_systemNonBlock = true;
}

void FdCtx::setTimeout(const int type, const u64 milliseconds) noexcept
{
    if (type == SO_RCVTIMEO)
    {
        m_recvTimeout.store(milliseconds, std::memory_order_release);
    }
    else if (type == SO_SNDTIMEO)
    {
        m_sendTimeout.store(milliseconds, std::memory_order_release);
    }
}

u64 FdCtx::getTimeout(const int type) const noexcept
{
    if (type == SO_RCVTIMEO)
    {
        return m_recvTimeout.load(std::memory_order_acquire);
    }
    if (type == SO_SNDTIMEO)
    {
        return m_sendTimeout.load(std::memory_order_acquire);
    }
    return INVALID64;
}

FdCtx::ptr FdManager::get(const int fd, const bool auto_create)
{
    if (fd < 0)
    {
        return nullptr;
    }
    {
        std::shared_lock lock(m_mutex);
        const auto it = m_contexts.find(fd);
        if (it != m_contexts.end())
        {
            return it->second;
        }
        if (!auto_create)
        {
            return nullptr;
        }
    }
    std::unique_lock lock(m_mutex);
    if (const auto it = m_contexts.find(fd); it != m_contexts.end()) // 再次查找
    {
        return it->second;
    }
    auto created = std::make_shared<FdCtx>(fd);
    m_contexts.emplace(fd, created);
    return created;
}

void FdManager::del(const int fd)
{
    std::unique_lock lock(m_mutex);
    m_contexts.erase(fd);
}

void FdManager::close(const int fd)
{
    FdCtx::ptr context;
    {
        std::unique_lock lock(m_mutex);
        if (const auto it = m_contexts.find(fd); it != m_contexts.end())
        {
            context = it->second;
            context->markClose();
            m_contexts.erase(it);
        }
    }
}

std::vector<FdCtx::ptr> FdManager::getAll() const
{
    std::shared_lock lock(m_mutex);
    std::vector<FdCtx::ptr> result;
    result.reserve(m_contexts.size());
    for (const auto& context: m_contexts | std::views::values)
    {
        result.push_back(context);
    }
    return result;
}
}
