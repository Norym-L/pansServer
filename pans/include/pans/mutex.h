#ifndef PANS_INCLUDE_PANS_MUTEX_H
#define PANS_INCLUDE_PANS_MUTEX_H

#include <atomic>
#include <mutex>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#elif defined(__arch64__) || defined(__arm__)
#include <arm_acle.h>
#endif

#if defined(__linux__) && defined(__GLIBC__)
#include <pthread.h>
#endif

inline void cpu_relex() noexcept //告诉cpu我在忙等减少超线程干扰
{
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(__arch64__) || defined(__arm__)
    __yield();
#endif
}

namespace pans
{

#if defined(__linux__) && defined(__GLIBC__)

    class Spinlock
    {
    public:
        Spinlock() noexcept
        {
            pthread_spin_init(&m_mutex, PTHREAD_PROCESS_SHARED);
        }

        ~Spinlock() noexcept
        {
            pthread_spin_destroy(&m_mutex);
        }

        Spinlock(const Spinlock&) = delete;
        Spinlock& operator=(const Spinlock&) = delete;

        void lock() noexcept
        {
            pthread_spin_lock(&m_mutex);
        }

        void unlock() noexcept
        {
            pthread_spin_unlock(&m_mutex);
        }

    private:
        pthread_spinlock_t m_mutex;
    };

#else

    class Spinlock
    {
        public:
        using lock_t = std::lock_guard<std::mutex>;

        Spinlock() noexcept = default;
        ~Spinlock() noexcept = default;

        Spinlock(const Spinlock&) = delete;
        Spinlock& operator=(const Spinlock&) = delete;

        void lock() noexcept
        {
            while (m_mutex.test_and_set(std::memory_order_acquire))
            {
                while (m_mutex.test(std::memory_order_relaxed))
                {
                    cpu_relex();
                }
            }
        }

        [[nodiscard]] bool try_lock() noexcept
        {
            return !m_mutex.test_and_set(std::memory_order_acquire);
        }

        void unlock() noexcept
        {
            m_mutex.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag m_mutex = ATOMIC_FLAG_INIT;
    };

#endif
}

#endif //PANS_INCLUDE_PANS_MUTEX_H











