#ifndef PANS_INCLUDE_PANS_IOMANAGER_H
#define PANS_INCLUDE_PANS_IOMANAGER_H

#include <pans/scheduler.h>
#include <pans/timer.h>

namespace pans
{
class PANS_API IOManager : public Scheduler, public TimerManager
{
public:
    using ptr = std::shared_ptr<IOManager>;

    enum class Event : u32
    {
        NONE = 0x0,
        READ = 0x1,
        WRITE = 0x4,
    };
    explicit IOManager(std::size_t thread = 1, bool use_caller = true, const std::string& name = "");
    ~IOManager() override;

    IOManager(const IOManager&) = delete;
    IOManager& operator=(const IOManager&) = delete;
    IOManager(IOManager&&) = delete;
    IOManager& operator=(IOManager&&) = delete;

    int addEvent(int fd, Event event, std::function<void()> callback = {});
    bool delEvent(int fd, Event event);
    /// 取消并触发事件
    bool cancelEvent(int fd, Event event);
    bool cancelAll(int fd);

    static bool CancelFdAcrossManagers(int fd);
    [[nodiscard]] static bool CancelEventAcrossManagers(int fd, Event event);

    [[nodiscard]] static IOManager* GetThis() noexcept;
    ///注册 当iomanager停止时，唤醒等待中的协程 的动作
    [[nodiscard]] u64 registerCancellationAction(std::function<void()> action);
    void unregisterCancellationAction(u64 key) noexcept;
    [[nodiscard]] bool isShuttingDown() const noexcept;

protected:
    void tickle() override;
    [[nodiscard]] bool canStop() override;
    void idle() override;
    void onTimerInsertedAtFront() override;

    [[nodiscard]] bool canStop(u64& timeout);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}

#endif // PANS_INCLUDE_PANS_IOMANAGER_H