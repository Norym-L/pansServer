#include <future>
#include <fcntl.h>

#include <pans/iomanager.h>

namespace
{
using namespace std::chrono_literals;

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

class Pipe final
{
public:
    Pipe()
    {
        if (::pipe2(m_fds, O_NONBLOCK | O_CLOEXEC) != 0)
        {
            throw std::runtime_error("pipe2 failed");
        }
    }

    ~Pipe()
    {
        ::close(m_fds[0]);
        ::close(m_fds[1]);
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    [[nodiscard]] int readFd() const noexcept { return m_fds[0]; }
    [[nodiscard]] int writeFd() const noexcept { return m_fds[1]; }

    void writeByte() const
    {
        constexpr char byte = 'x';
        Check(::write(m_fds[1], &byte, sizeof(byte)) == 1, "pipe write failed");
    }

    void readByte() const
    {
        char byte = 0;
        Check(::read(m_fds[0], &byte, sizeof(byte)) == 1, "pipe read failed");
    }

private:
    int m_fds[2]{-1, -1}; // 0读 1写
};

void TestFunc1()
{
    pans::IOManager iom(2, false, "main");
    const Pipe pipe;
    std::promise<void> callback_down;
    auto callback_future = callback_down.get_future();
    Check(iom.addEvent(pipe.readFd(), pans::IOManager::Event::READ, [&]()
                           {
                               pipe.readByte();
                               callback_down.set_value();
                           }) == 0, "could not register callback event1");
    pipe.writeByte();
    WaitReady(callback_future, "read callback event time out");

    std::atomic<int> deleted_callback{0};
    Check(iom.addEvent(pipe.readFd(), pans::IOManager::Event::READ, [&]()
                           {
                               deleted_callback.fetch_add(1);
                           }) == 0, "could not register callback event2");
    Check(iom.delEvent(pipe.readFd(), pans::IOManager::Event::READ), "event deletion failed");
    Check(!iom.delEvent(pipe.readFd(), pans::IOManager::Event::READ), "event was deleted twice");
    pipe.writeByte();
    std::this_thread::sleep_for(5ms);
    Check(deleted_callback.load() == 0, "deleted event callback executed");
    iom.stop();
}

void TestFunc2()
{
    pans::IOManager caller_manager(1, true, "caller1");
    std::atomic<bool> caller_timer_fired{false};
    caller_manager.addTimer(2ms, [&]() { caller_timer_fired.store(true); });
    caller_manager.stop();
    Check(caller_timer_fired.load(), " caller-mode stop did not drain its timer");

    pans::IOManager restarted(2, false, "io_restart");
    restarted.stop();
    restarted.start();
    std::promise<void> restarted_timer;
    auto restarted_future = restarted_timer.get_future();
    restarted.addTimer(2ms, [&]() { restarted_timer.set_value(); });
    WaitReady(restarted_future, "restarted IOManager did not execute its timer");
    restarted.stop();
}

void TestFunc3()
{
    pans::IOManager iom(2, false, "main");
    const Pipe pipe;
    std::promise<void> registered;
    auto registered_future = registered.get_future();
    std::promise<void> resumed;
    auto resumed_future = resumed.get_future();
    iom.addTask([&]()
        {
            Check(pans::IOManager::GetThis() == &iom, "task lost its IOManager context");
            Check(iom.addEvent(pipe.readFd(), pans::IOManager::Event::READ) == 0, "could not register fiber event");
            registered.set_value();
            pans::Fiber::YieldToHold(pans::Fiber::WaitReason::IO);
            pipe.readByte();
            resumed.set_value();
        });
    WaitReady(registered_future, "Fiber did not register its IO event");
    pipe.writeByte();
    WaitReady(resumed_future, "IO event did not resume its Fiber");
    iom.stop();
}

void TestFunc4()
{
    pans::IOManager iom(2, false, "main");
    const Pipe pipe;
    std::promise<void> canceled;
    auto canceled_future = canceled.get_future();
    Check(iom.addEvent(pipe.readFd(), pans::IOManager::Event::READ, [&]()
                           {
                               canceled.set_value();
                           }) == 0, "could not register cancelable event");
    Check(iom.cancelEvent(pipe.readFd(), pans::IOManager::Event::READ), "cancelEvent failed");
    WaitReady(canceled_future, "cancelEvent did not dispatch its callback");

    std::promise<void> canceled_all;
    auto canceled_all_future = canceled_all.get_future();
    Check(iom.addEvent(pipe.readFd(), pans::IOManager::Event::READ, [&]()
                           {
                               canceled_all.set_value();
                           }) == 0, "could not register event for cancelAll");
    Check(iom.cancelAll(pipe.readFd()), "cancelAll failed");
    WaitReady(canceled_all_future, "cancelAll did not dispatch its callback");
    Check(!iom.cancelAll(pipe.readFd()), "cancelAll succeeded without registered events");

    std::promise<void> one_shot_done;
    auto one_shot_future = one_shot_done.get_future();
    iom.addTimer(5ms, [&]() { one_shot_done.set_value(); });
    WaitReady(one_shot_future, "epoll did not wake for a one-shot timer");
    std::atomic<int> recurring_count{0};
    std::promise<void> recurring_done;
    auto recurring_future = recurring_done.get_future();
    // 循环timer测试
    pans::Timer::ptr timer = iom.addTimer(2ms,
                                          [&]()
                                              {
                                                  if (recurring_count.fetch_add(1) + 1 == 3)
                                                  {
                                                      Check(timer->cancel(), "IOManager recurring timer could not cancel itself");
                                                      recurring_done.set_value();
                                                  }
                                              },
                                          true);
    WaitReady(recurring_future, "recurring IOManager timer did not complete");
    Check(recurring_count.load() == 3, "recurring timer executed the wrong number of timers");
    iom.stop();
}
}

int main()
{
    TestFunc1();
    TestFunc2();
    TestFunc3();
    TestFunc4();

    return EXIT_SUCCESS;
}
