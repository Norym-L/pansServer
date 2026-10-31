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
        INIT,    // 上下文就绪但是没执行
        READY,   // 准备执行
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

    // 转入执行协程
    State resume();
    // 重置协程
    void reset(Callback callback);
    // 把 WAITING 状态改为 READY
    [[nodiscard]] bool wake() noexcept;
    [[nodiscard]] u64 getId() const noexcept;
    [[nodiscard]] State getState() const noexcept;
    [[nodiscard]] WaitReason getWaitReason() const noexcept;
    [[nodiscard]] std::size_t getStackSize() const noexcept;
    [[nodiscard]] bool hasGuardPage() const noexcept;
    [[nodiscard]] std::exception_ptr getException() const noexcept;
    // 重抛异常
    void rethrowFailed() const;

    [[nodiscard]] static Fiber* Current() noexcept;
    [[nodiscard]] static ptr GetThis();
    [[nodiscard]] static u64 GetFiberId() noexcept;
    [[nodiscard]] static u64 TotalFibers() noexcept;

    static void YteldToReady();
    static void YieldToHold(WaitReason reason = WaitReason::External);

    [[nodiscard]] static std::string_view StateName(State state) noexcept;
    [[nodiscard]] static std::string_view WaitReasonName(WaitReason reason) noexcept;

private:
    // 初始化协程上下文
    void initializeContext(Callback callback);
    // 执行 协程栈callback
    void run();
    // 主动挂起让出cpu
    void yied(State target, WaitReason reason);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}

#endif //PANS_INCLUDE_PANS_FIBER_H
