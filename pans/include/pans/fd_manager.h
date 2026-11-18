#ifndef PANS_INCLUDE_PANS_FD_MANAGER_H
#define PANS_INCLUDE_PANS_FD_MANAGER_H

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <pans/export.h>
#include <pans/singleton.h>
#include <pans/types.h>

namespace pans
{
class PANS_API FdCtx final
{
public:
    using ptr = std::shared_ptr<FdCtx>;
    explicit FdCtx(int fd);
    FdCtx(const FdCtx&) = delete;
    FdCtx& operator=(const FdCtx&) = delete;

    [[nodiscard]] bool isInit() const noexcept { return m_initialized; }

    [[nodiscard]] bool isSocket() const noexcept { return m_socket; }

    [[nodiscard]] bool isClose() const noexcept { return m_closed.load(std::memory_order_acquire); }

    void setUserNonBlock(const bool value) noexcept { m_userNonBlock.store(value, std::memory_order_release); }

    [[nodiscard]] bool getUserNonBlock() const noexcept { return m_userNonBlock.load(std::memory_order_acquire); }

    [[nodiscard]] bool getSysNonBlock() const noexcept { return m_systemNonBlock; }

    void setTimeout(int type, u64 milliseconds) noexcept;

    [[nodiscard]] u64 getTimeout(int type) const noexcept;

    /**
     * 取消事件并递增版本号
     * @param reading 是否为读事件
     */
    void cancelWait(const bool reading) noexcept { (reading ? m_readCancelEpoch : m_writeCancelEpoch).fetch_add(1, std::memory_order_acq_rel); }

    [[nodiscard]] u64 getCancelEpoch(const bool reading) noexcept { return (reading ? m_readCancelEpoch : m_writeCancelEpoch).load(std::memory_order_acquire); }

    [[nodiscard]] int getFd() const noexcept { return m_fd; }

    void markClose() noexcept { m_closed.store(true, std::memory_order_release); }

private:
    void initialize() noexcept;

private:
    std::atomic<u64> m_recvTimeout{INVALID64};
    std::atomic<u64> m_sendTimeout{INVALID64};
    std::atomic<u64> m_readCancelEpoch{0};  // 读版本号，触发时间时判断，如果不一样则是主动取消的事件
    std::atomic<u64> m_writeCancelEpoch{0}; // 写版本号
    int m_fd{-1};
    bool m_initialized{false};
    bool m_socket{false};
    bool m_systemNonBlock{false};            // 系统设置为非阻塞
    std::atomic<bool> m_userNonBlock{false}; // 用户设置为非阻塞
    std::atomic<bool> m_closed{false};
};

class PANS_API FdManager final : public Singleton<FdManager>
{
    friend class Singleton;

public:
    FdManager(const FdManager&) = delete;
    FdManager& operator=(const FdManager&) = delete;
    FdManager(FdManager&&) = delete;
    FdManager& operator=(FdManager&&) = delete;

    [[nodiscard]] FdCtx::ptr get(int fd, bool auto_create = false);

    void del(int fd);

    void close(int fd);

    [[nodiscard]] std::vector<FdCtx::ptr> getAll() const;

private:
    FdManager() = default;
    ~FdManager() = default;

private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<int, FdCtx::ptr> m_contexts;
};
}

#endif //PANS_INCLUDE_PANS_FD_MANAGER_H
