#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <pans/fiber.h>
#include <pans/macros.h>

namespace
{
// 协程挂起重启
void TestFiber1()
{
    std::vector<int> steps;
    pans::Fiber::ptr fiber = std::make_shared<pans::Fiber>([&]()
        {
            ASSERT_RETNONE2(pans::Fiber::Current() == fiber.get(), "Current() did not return running fiber");
            ASSERT_RETNONE2(pans::Fiber::GetThis() == fiber, "GetThis() did not preserve shared ownership");
            ASSERT_RETNONE2(pans::Fiber::GetFiberId() == fiber->getId(), "running fiber id mismatch");
            steps.push_back(1);
            pans::Fiber::YieldToReady();
            steps.push_back(3);
        });
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::INIT, "new fiber must start in INIT");
    fiber->resume();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::READY, "yielded fiber must be READY");
    steps.push_back(2);
    fiber->resume();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::TERM, "completed fiber must be TERM");
    ASSERT_RETNONE2(steps == std::vector<int>({1,2,3}), "fiber execution order mismatch");
    ASSERT_RETNONE2(pans::Fiber::Current() == nullptr, "caller must be reported as a fiber");
    ASSERT_RETNONE2(pans::Fiber::GetFiberId() == 0, "caller fiber id must be 0");
}

// 协程挂起唤醒
void TestFiber2()
{
    int executions = 0; // 协程执行次数
    auto fiber = std::make_shared<pans::Fiber>([&]()
        {
            ++executions;
            pans::Fiber::YieldToHold(pans::Fiber::WaitReason::IO);
            ++executions;
        });
    fiber->resume();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::WAITING, "held fiber must be WAITING");
    ASSERT_RETNONE2(fiber->getWaitReason() == pans::Fiber::WaitReason::IO, "wait reson was not retained");
    bool rejected = false;
    try
    {
        fiber->resume();
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    ASSERT_RETNONE2(rejected, "WAITING fiber resumed without wake()");
    std::atomic<int> successful_wakes{0};
    std::vector<std::thread> wake_threads;
    for (auto index = 0; index < 8; ++index)
    {
        wake_threads.emplace_back([&]()
            {
                if (fiber->wake())
                {
                    successful_wakes.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }
    for (auto& t: wake_threads)
    {
        t.join();
    }
    ASSERT_RETNONE2(successful_wakes.load(std::memory_order_acquire) == 1, "never, just allow one thread to wake() fiber");
    ASSERT_RETNONE2(fiber->getWaitReason() == pans::Fiber::WaitReason::None, "wake() must clear wait reason");
    fiber->resume();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::TERM, "woken fiber did not terminate");
    ASSERT_RETNONE2(executions == 2, "woken fiber did not resume at suspension point");
}

// 协程异常处理与重置
void TestFiber3()
{
    auto fiber = std::make_shared<pans::Fiber>([]()
        {
            throw std::runtime_error("fiber failure");
        });

    const std::uint64_t id = fiber->getId();
    fiber->resume();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::EXCEPT, "throwing fiber must be EXCEPT");
    ASSERT_RETNONE2(fiber->getException() != nullptr, "fiber exception was not retained");
    bool rethrown = false;
    try
    {
        fiber->rethrowIfFailed();
    }
    catch (const std::runtime_error& e)
    {
        rethrown = std::string(e.what()) == "fiber failure";
    }
    ASSERT_RETNONE2(rethrown, "original fiber exception was not rethrown");
    bool reset_falg = false;
    fiber->reset([&]()
        {
            reset_falg = true;
        });
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::INIT, "rest fiber must return to INIT");
    ASSERT_RETNONE2(fiber->getException() == nullptr, "rest fiber retained old exception");
    ASSERT_RETNONE2(fiber->getId() == id, "reset must preserve fiber identity");
    fiber->resume();
    ASSERT_RETNONE2(reset_falg && fiber->getState() == pans::Fiber::State::TERM, "reset task did not complete");
}

// 非法参数构造协程
void TestFiber4()
{
    bool invalid_size_rejected = false;
    try
    {
        pans::Fiber::Options invalid_options;
        invalid_options.stackSize = 1;
        auto invalid = std::make_shared<pans::Fiber>([]() {}, invalid_options);
    }
    catch (const std::invalid_argument&)
    {
        invalid_size_rejected = true;
    }
    ASSERT_RETNONE2(invalid_size_rejected, "invalid stack size was accepted");
    pans::Fiber::Options options;
    options.stackSize = pans::Fiber::DEFAULT_STACK_SIZE * 2;
    options.guardPage = false;
    auto fiber = std::make_shared<pans::Fiber>([]() {}, options);
    ASSERT_RETNONE2(fiber->getStackSize() == options.stackSize, "custom stack size was not retained");
    ASSERT_RETNONE2(fiber->hasGuardPage() == options.guardPage, "gard-page option was not retained");
    fiber->resume();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::TERM, "custom-stack did not terminate");
}

// 嵌套协程
void TestFiber5()
{
    std::vector<int> steps{};
    pans::Fiber::ptr outer = std::make_shared<pans::Fiber>([&]()
        {
            steps.push_back(1);
            auto innner = std::make_shared<pans::Fiber>([&]()
                {
                    steps.push_back(3);
                    pans::Fiber::YieldToReady();
                    steps.push_back(5);
                });
            pans::Fiber::YieldToReady();
            innner->resume();
            ASSERT_RETNONE2(pans::Fiber::Current() == outer.get(), "nested yield did not restore outer fiber");
            steps.push_back(4);
            innner->resume();
            ASSERT_RETNONE2(innner->getState() == pans::Fiber::State::TERM, "nested fiber did not terminate");
            steps.push_back(6);
        });
    outer->resume();
    steps.push_back(2);
    outer->resume();
    ASSERT_RETNONE2(outer->getState() == pans::Fiber::State::TERM, "outer fiber did not ternimate");
    ASSERT_RETNONE2(steps == std::vector<int>({1,2,3,4,5,6}), "nested caller switching is incorrect");
}

struct DestructionProbe
{
    explicit DestructionProbe(std::atomic<int>& destructions)
        : count(destructions)
    {}

    ~DestructionProbe()
    {
        count.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int>& count;
};

// 析构挂起状态的协程
void TestFiber6()
{
    std::atomic<int> destructions{0};
    auto fiber = std::make_shared<pans::Fiber>([&]()
        {
            DestructionProbe probe(destructions);
            pans::Fiber::YieldToHold(); // 清除 shared_ptr 会抛出boost异常，context会自动捕获异常并完成资源释放
        });
    fiber->resume();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::WAITING, "fiber did not suspend befroe destruction");
    fiber.reset(); // 清空 std::shared_ptr，按照预期会自动析构 fiber栈 上的资源
    ASSERT_RETNONE2(destructions.load(std::memory_order_relaxed) == 1, "fatal: faiber did not unwind stack objects");
}

// 协程跨线程执行
void TestFiber7()
{
    std::thread::id first_thread;
    std::thread::id second_thread;
    auto fiber = std::make_shared<pans::Fiber>([&]()
        {
            first_thread = std::this_thread::get_id();
            pans::Fiber::YieldToReady();
            second_thread = std::this_thread::get_id();
        });
    fiber->resume();
    std::thread worker([fiber]()
        {
            fiber->resume();
        });
    worker.join();
    ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::TERM, "migrated fiber did not terminate");
    ASSERT_RETNONE2(first_thread != second_thread, "fiber did not resume on the worker thread");
}

// 安全交接上下文（parked）
void TestFiber8()
{
    for (int i = 0; i < 1000; ++i)
    {
        std::atomic<bool> entered{false};
        std::atomic<bool> completed{false};
        auto fiber = std::make_shared<pans::Fiber>([&]()
            {
                entered.store(true, std::memory_order_release);
                pans::Fiber::YieldToReady();
                completed.store(true, std::memory_order_release);
            });
        std::thread first_resume([fiber]()
            {
                fiber->resume();
            });
        while (!entered.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        while (fiber->getState() != pans::Fiber::State::READY)
        {
            std::this_thread::yield();
        }
        fiber->resume();
        first_resume.join();
        ASSERT_RETNONE2(completed.load(std::memory_order_acquire), "concurrent context handoff lost execution");
        ASSERT_RETNONE2(fiber->getState() == pans::Fiber::State::TERM, "concurrent context handoff did not terminate");
    }
}
}

int main()
{
    const std::uint64_t baseline = pans::Fiber::TotalFibers();
    TestFiber1();
    TestFiber2();
    TestFiber3();
    TestFiber4();
    TestFiber5();
    TestFiber6();
    TestFiber7();
    TestFiber8();

    ASSERT_RETVAL2(pans::Fiber::TotalFibers() == baseline, EXIT_FAILURE, "fiber lifetime counter leaked");
}
