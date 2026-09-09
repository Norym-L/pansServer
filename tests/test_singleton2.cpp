#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>

#define SINGLETON_VERSION_1 1
#define SINGLETON_VERSION_2 2
#define SINGLETON_VERSION_3 3
#define SINGLETON_VERSION_4 4
#define SINGLETON_VERSION_5 5

#ifndef SINGLETON_VERSION
#define SINGLETON_VERSION SINGLETON_VERSION_1
#endif

#if SINGLETON_VERSION == SINGLETON_VERSION_1
// 非线程安全版本
class Singleton
{
public:
    static Singleton *Instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new Singleton();
        }
        return m_instance;
    }

    void log(const std::string &msg)
    {
        std::cout << "[LOG]: " << msg << std::endl;
    }

    Singleton(const Singleton &) = delete;
    Singleton(Singleton &&) = delete;
    Singleton &operator=(const Singleton &) = delete;
    Singleton &operator=(Singleton &&) = delete;

private:
    Singleton() = default;
    ~Singleton() = default;

private:
    inline static Singleton *m_instance = nullptr;
};

int main()
{
    Singleton::Instance()->log("Hello Singleton.1");
    return 0;
}

#elif SINGLETON_VERSION == SINGLETON_VERSION_2
// 这个版本能保证单例，也线程安全，但是每次都锁，效率不高
class Singleton
{
public:
    static Singleton *Instance()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_instance == nullptr)
        {
            m_instance = new Singleton();
        }
        return m_instance;
    }

    void log(const std::string &msg)
    {
        std::cout << "[LOG]: " << msg << std::endl;
    }

    Singleton(const Singleton &) = delete;
    Singleton(Singleton &&) = delete;
    Singleton &operator=(const Singleton &) = delete;
    Singleton &operator=(Singleton &&) = delete;

private:
    Singleton() = default;
    ~Singleton() = default;

private:
    inline static Singleton *m_instance = nullptr;
    inline static std::mutex m_mutex;
};

int main()
{
    Singleton::Instance()->log("Hello Singleton.2");
    return 0;
}

#elif SINGLETON_VERSION == SINGLETON_VERSION_3
// 双检查锁，无法应对编译优化的指令重排(Reorder)
// 线程可能会拿到一个没有经过初始化的单例对象，不安全
class Singleton
{
public:
    static Singleton *Instance()
    {
        if (m_instance == nullptr)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_instance == nullptr)
            {
                m_instance = new Singleton(); // 现代cpu可能会延后构造对象，而立马返回对象指针
            }
        }
        return m_instance;
    }

    void log(const std::string &msg)
    {
        std::cout << "[LOG]: " << msg << std::endl;
    }

    Singleton(const Singleton &) = delete;
    Singleton(Singleton &&) = delete;
    Singleton &operator=(const Singleton &) = delete;
    Singleton &operator=(Singleton &&) = delete;

private:
    Singleton() = default;
    ~Singleton() = default;

private:
    inline static Singleton *m_instance = nullptr;
    inline static std::mutex m_mutex;
};

int main()
{
    Singleton::Instance()->log("Hello Singleton.3");
    return 0;
}

#elif SINGLETON_VERSION == SINGLETON_VERSION_4
// 接近最终解了，保证单例，线程安全，效率也足够好，也能规避指令重排的风险
// 但是还缺少对象释放的过程，这个单例是没有对象释放环节的
class Singleton
{
public:
    static Singleton *Instance()
    {
        Singleton *temp = m_instance.load(std::memory_order_acquire);
        if (temp == nullptr)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            temp = m_instance.load(std::memory_order_relaxed);
            if (temp == nullptr)
            {
                temp = new Singleton();
                m_instance.store(temp, std::memory_order_release);
            }
        }
        return temp;
    }

    void log(const std::string &msg)
    {
        std::cout << "[LOG]: " << msg << std::endl;
    }

    Singleton(const Singleton &) = delete;
    Singleton(Singleton &&) = delete;
    Singleton &operator=(const Singleton &) = delete;
    Singleton &operator=(Singleton &&) = delete;

private:
    Singleton() = default;
    ~Singleton() = default;

private:
    inline static std::atomic<Singleton *> m_instance{};
    inline static std::mutex m_mutex;
};

int main()
{
    Singleton::Instance()->log("Hello Singleton.4");
    return 0;
}

#else
// 比较理想的版本
template<typename T>
class Singleton
{
public:
    static T *Instance()
    {
        T *temp = m_instance.load(std::memory_order_acquire);
        if (temp == nullptr)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            temp = m_instance.load(std::memory_order_relaxed);
            if (temp == nullptr)
            {
                temp = new T();
                // 只有成功创建实例后才注册退出清理器，并且必须在发布指针前完成。
                RegisterCleanup();
                m_instance.store(temp, std::memory_order_release);
            }
        }
        return temp;
    }

    Singleton(const Singleton &) = delete;
    Singleton(Singleton &&) = delete;
    Singleton &operator=(const Singleton &) = delete;
    Singleton &operator=(Singleton &&) = delete;

protected:
    Singleton() = default;
    ~Singleton() = default;

private:
    static void Destroy() noexcept
    {
        // 先撤回已发布的指针，再销毁对象，避免留下悬空的原子指针。
        T *instance = m_instance.exchange(nullptr, std::memory_order_acq_rel);
        delete instance;
    }

    class Cleanup
    {
    public:
        Cleanup() = default;
        Cleanup(const Cleanup &) = delete;
        Cleanup &operator=(const Cleanup &) = delete;

        ~Cleanup()
        {
            Singleton<T>::Destroy();
        }
    };

    static void RegisterCleanup()
    {
        // 函数局部静态对象只初始化一次，并在正常退出程序时自动析构。
        static Cleanup cleanup;
        (void) cleanup;
    }

    inline static std::atomic<T *> m_instance{};
    inline static std::mutex m_mutex;
};

class A : public Singleton<A>
{
    friend class Singleton<A>;

public:
    void log(const std::string &msg)
    {
        std::cout << "[LOG]: " << msg << " data=" << m_data << std::endl;
    }

private:
    // 用可观察输出验证构造和析构都只发生一次。
    A()
    {
        std::puts("A::A() -- only once");
        m_data++;
    }

    ~A()
    {
        std::puts("A::~A() -- only once");
    }

private:
    int m_data{0};
};

int main()
{
    std::vector<std::thread> threads;
    threads.reserve(8);

    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([]
        {
            A::Instance()->log("Hello Meyers Singleton.5");
        });
    }

    for (auto &thread: threads)
    {
        thread.join();
    }
}

#endif
