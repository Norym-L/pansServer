#include <string>
#include <thread>
#include <iostream>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "pans/utils/thread_uitls.h"

int main()
{
    std::cout << "1: main thread id: " << pans::GetThreadId() << std::endl;
    std::cout << "2: main thread name: " << pans::GetThreadName() << std::endl;
    sleep(30); // top -H -P <pid>
    pans::SetThreadName("main_thread");
    std::cout << "3: main thread name: " << pans::GetThreadName() << std::endl;
    sleep(5);
    pans::SetThreadName("线程设汉字");
    std::cout << "4: main thread name: " << pans::GetThreadName() << std::endl;
    sleep(5);
    pans::SetThreadName("线程设超级长的汉字字符串");
    std::cout << "5: main thread name: " << pans::GetThreadName() << std::endl;
    sleep(5);
    std::thread woker([]()
    {
        std::cout << "6: son thread id: " << pans::GetThreadId() << std::endl;
        std::cout << "7: son hread name: " << pans::GetThreadName() << std::endl;
        sleep(5);
        pans::SetThreadName("son-thread-name-longggggggg");
        std::cout << "8: son hread name: " << pans::GetThreadName() << std::endl;
        sleep(5);
        pans::SetThreadName(""); // 失败日志
        std::cout << "9: son hread name: " << pans::GetThreadName() << std::endl;
    });
    woker.join();
}
