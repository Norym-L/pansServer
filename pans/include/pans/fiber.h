#ifndef PANS_INCLUDE_PANS_FIBER_H
#define PANS_INCLUDE_PANS_FIBER_H

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string_view>

#include <pans/export.h>
#include <pans/types.h>

namespace pans
{
class PANS_API Fiber final : public std::enable_shared_from_this<Fiber>
{
public:
    using ptr = std::shared_ptr<Fiber>;
    using Callback = std::function<void()>;

    enum class State: u8
    {
        INIT,    // 任务没开始
        READY,   // 准备执行 或是 挂起状态恢复准备执行
        EXEC,    // 执行中
        WAITING, // 等待内部步骤执行
        TERM,    // 协程正常终止
        EXCEPT,  // 出现异常
    };

    enum class WaitReason : u8
    {
        None,      // 没有等待
        IO,        // io
        Timer,     // 计时器
        Semaphore, // 信号量
        External,  // 外部
    };

    static constexpr std::size_t DEFAULT_STACK_SIZE = 128U * 1024U; // 128k 协程栈
    struct Options
    {
        std::size_t stackSize = DEFAULT_STACK_SIZE;
        bool guardPage = true;
    };

    explicit Fiber(Callback callback);
    Fiber(Callback callback, Options options);

    ~Fiber() noexcept;

    Fiber(const Fiber&) = delete;
    Fiber operator=(const Fiber&) = delete;
    Fiber(Fiber&&) = delete;
    Fiber operator=(Fiber&&) = delete;

    /**
     * 运行协程 <br>
     * 中途挂起不经过 wake() 会抛出 std::logic_error
     */
    State resume();
    // 重置协程
    void reset(Callback callback) const;
    // 唤醒 WAITING 状态的协程
    [[nodiscard]] bool wake() const noexcept;
    [[nodiscard]] u64 getId() const noexcept;
    [[nodiscard]] State getState() const noexcept;
    [[nodiscard]] WaitReason getWaitReason() const noexcept;
    [[nodiscard]] std::size_t getStackSize() const noexcept;
    [[nodiscard]] bool hasGuardPage() const noexcept;
    [[nodiscard]] std::exception_ptr getException() const noexcept;
    // 重抛异常
    void rethrowIfFailed() const;

    /**
     * 获取当前协程指针
     * @return pans::Fiber*
     */
    [[nodiscard]] static Fiber* Current() noexcept;
    /**
     * 在协程内部获取指向自己的共享指针
     * @return std::shared_ptr<pans::Fiber>
     */
    [[nodiscard]] static ptr GetThis();
    [[nodiscard]] static u64 GetFiberId() noexcept;
    [[nodiscard]] static u64 TotalFibers() noexcept;

    /**
     * 主动挂起，把CPU让给调用者（等待下次自动执行）
     */
    static void YieldToReady();
    /**
     * 主动挂起，把CPU让给调用者（不会自动执行）
     * @param reason 挂起原因
     */
    static void YieldToHold(WaitReason reason = WaitReason::External);

    [[nodiscard]] static std::string_view StateName(State state) noexcept;
    [[nodiscard]] static std::string_view WaitReasonName(WaitReason reason) noexcept;

private:
    // 初始化协程上下文
    void initializeContext(Callback callback) const;
    // 执行 协程栈callback
    void run() const;
    // 主动挂起让出cpu
    void yied(State target, WaitReason reason) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}

#endif //PANS_INCLUDE_PANS_FIBER_H
