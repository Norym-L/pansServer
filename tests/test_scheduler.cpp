#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <functional>

#include <pans/scheduler.h>
#include <pans/utils/thread_uitls.h>

namespace
{
using namespace std::chrono_literals;
std::atomic<int> g_functionCounter{0};

void FunctionTask()
{
    g_functionCounter.fetch_add(1, std::memory_order_relaxed);
}

void Check(const bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template<typename Future>
void WaitReady(Future& future, const char* message)
{
    if (future.wait_for(5s) != std::future_status::ready)
    {
        throw std::runtime_error(message);
    }
}
}

void TestFunc1()
{
    std::atomic<int> callback_count{0};
    std::atomic<int> fiber_steps{0};
    std::promise<void> fiber_done;
    std::future<void> fiber_done_future = fiber_done.get_future();

    pans::Scheduler scheduler(3, false, "main");
    scheduler.start();
    scheduler.addTask(&FunctionTask);
    for (int index = 0; index < 64; ++index)
    {
        scheduler.addTask([&callback_count]()
            {
                Check(pans::Scheduler::GetThis() != nullptr, "callback lost its scheduler context");
                callback_count.fetch_add(1, std::memory_order_relaxed);
            });
    }
    scheduler.addTask([&]()
        {
            for (int step = 0; step < 3; ++step)
            {
                fiber_steps.fetch_add(1, std::memory_order_relaxed);
                if (step != 2)
                {
                    pans::Fiber::YieldToReady();
                }
            }
            fiber_done.set_value();
        });
    WaitReady(fiber_done_future, "READY fiber was not rescheduled");
    scheduler.stop();
    Check(callback_count.load(std::memory_order_acquire) == 64, "scheduler lost callbacks");
    Check(fiber_steps.load(std::memory_order_acquire) == 3, "READY fiber run the wrong number of times");
    Check(g_functionCounter.load(std::memory_order_acquire) == 1, "legacy function-pointer task did not execute");
}

void TestFunc2()
{
    std::promise<pans::Fiber::ptr> suspended;
    auto suspended_futrue = suspended.get_future();
    std::promise<void> resumed;
    auto resumed_futrue = resumed.get_future();

    pans::Scheduler scheduler(2, false, "main");
    scheduler.start();

    scheduler.addTask([&]()
        {
            suspended.set_value(pans::Fiber::GetThis());
            pans::Fiber::YieldToHold(pans::Fiber::WaitReason::External);
            resumed.set_value();
        });

    WaitReady(suspended_futrue, "fiber did not publish its waiting handle");
    pans::Fiber::ptr fiber = suspended_futrue.get();
    const auto waiting_deadline = std::chrono::steady_clock::now() + 5s;
    while (fiber->getState() != pans::Fiber::State::WAITING && std::chrono::steady_clock::now() < waiting_deadline)
    {
        std::this_thread::sleep_for(100us);
    }
    Check(fiber->getState() == pans::Fiber::State::WAITING, "fiber did not enter WAITING");

    scheduler.addTask(fiber);
    WaitReady(resumed_futrue, "explicit scheduled WAITING fiber did not resume");

    scheduler.stop();
    Check(fiber->getState() == pans::Fiber::State::TERM, "resumed fiber did not terminate");
}

void TestFunc3()
{
    pans::Scheduler scheduler(2, false, "main");
    scheduler.start();
    std::promise<pans::Scheduler::ThreadId> selected_thread;
    auto selected_thread_future = selected_thread.get_future();

    scheduler.addTask([&selected_thread]() { selected_thread.set_value(pans::GetThreadId()); });

    WaitReady(selected_thread_future, "could not discover worker thread");
    const pans::Scheduler::ThreadId thread_id = selected_thread_future.get();
    constexpr int TASK_COUNT = 32;
    std::atomic<int> completed{0};
    std::promise<void> all_done;
    auto all_done_future = all_done.get_future();
    for (int index = 0; index < TASK_COUNT; ++index)
    {
        scheduler.addTask([&]()
                              {
                                  Check(pans::GetThreadId() == thread_id, "thread-affine task ran on the wrong worker");
                                  if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 == TASK_COUNT)
                                  {
                                      all_done.set_value();
                                  }
                              },
                          thread_id);
    }
    WaitReady(all_done_future, "thread-affine tasks time out");
    scheduler.stop();

    std::promise<void> restarted;
    auto restarted_future = restarted.get_future();
    scheduler.start();
    scheduler.addTask([&restarted]() { restarted.set_value(); });
    WaitReady(restarted_future, "restarted scheduler did not execute tasks");
    scheduler.stop();
}

void TestFunc4()
{
    pans::Scheduler first(1, false, "first");
    pans::Scheduler second(1, false, "second");
    std::promise<void> finished;
    auto finished_future = finished.get_future();
    std::atomic<u64> first_thread{0};
    std::atomic<u64> second_thread{0};
    std::atomic<u64> returned_thread{0};

    first.start();
    second.start();
    first.addTask([&]()
        {
            first_thread.store(pans::GetThreadId(), std::memory_order_relaxed);
            {
                pans::SchedulerSwitcher switcher(&second);
                Check(pans::Scheduler::GetThis() == &second, "SchedulerSwitcher did not enter target scheduler");
                second_thread.store(pans::GetThreadId(), std::memory_order_relaxed);
            }
            Check(pans::Scheduler::GetThis() == &first, "SchedulerSwitcher did not return source scheduler");
            returned_thread.store(pans::GetThreadId(), std::memory_order_relaxed);
            finished.set_value();
        });

    WaitReady(finished_future, "SchdulerSwitcher migration timed out");
    first.stop();
    second.stop();
    Check(first_thread.load(std::memory_order_acquire) != second_thread.load(std::memory_order_acquire), "fiber did not migrate to second scheduler");
    Check(first_thread.load(std::memory_order_acquire) == returned_thread.load(std::memory_order_acquire), "fiber did not return to first scheduler");
}

void TestFunc5()
{
    pans::Scheduler scheduler(1, true, "caller");
    std::atomic<int> executions{0};
    std::vector<std::function<void()> > callbacks;
    callbacks.emplace_back([&]()
        {
            executions.fetch_add(1, std::memory_order_relaxed);
        });
    callbacks.emplace_back([&]()
        {
            throw std::runtime_error("expected scheduler test failure");
        });
    callbacks.emplace_back([&]()
        {
            executions.fetch_add(1, std::memory_order_relaxed);
        });
    Check(pans::Scheduler::GetThis() == &scheduler, "caller scheduler was not installed on its construction thread");
    Check(pans::Scheduler::GetMainFiber() != nullptr, "caller scheduler did not expose its root fiber");
    scheduler.start();
    scheduler.addTask(callbacks.begin(), callbacks.end());
    scheduler.stop(); // 启用 use_caller 会在 stop() 时唤醒主协程
    Check(executions.load(std::memory_order_acquire) == 2, "execution stopped caller scheduler task draining");
}

int main()
{
    TestFunc1();
    TestFunc2();
    TestFunc3();
    TestFunc4();
    TestFunc5();

    return EXIT_SUCCESS;
}
