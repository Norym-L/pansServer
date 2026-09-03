#include <iostream>
#include <thread>
#include <vector>

#include <pans/singleton.hpp>

class A : public pans::Singleton<A>
{
    friend class pans::Singleton<A>;
public:
    void log(const std::string &msg)
    {
        std::cout << "[LOG]: " << msg << " m_data=" << m_data << std::endl;
    }
private:
    // A() = default;
    // ~A() = default; 使用默认构造、析构函数，编译器不会对这个类加锁

    A()
    {
        std::puts("A:A() -- only once");
        m_data++;
    }
    ~A()
    {
        std::puts("A:~A() -- only once");
    }
private:
    int m_data{0};
};

int main()
{
    std::vector<std::thread> threads;
    threads.reserve(7);
    for (int i = 0; i < 7; ++i)
    {
        threads.emplace_back([]()
        {
            A::Instance().log("Hello, Meyers Singlton");
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
}
