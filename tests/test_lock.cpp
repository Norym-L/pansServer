#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <latch>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <vector>

constexpr int RUNS = 7;

uint64_t g_target = 0;
uint64_t g_Counter = 0;
std::atomic<uint64_t> g_atomicCounter(0);
std::mutex g_mutex;
std::shared_mutex g_sharedMutex;

uint64_t OperationsForThread(std::size_t thread_index, std::size_t thread_count)
{
    const uint64_t base = g_target / thread_count;
    const uint64_t remainder = g_target % thread_count;
    return base + (thread_index < remainder ? 1 : 0);
}

template<typename Operation>
std::chrono::nanoseconds BenchmarkThreads(std::size_t thread_count, Operation &&operation)
{
    std::latch ready(static_cast<std::ptrdiff_t>(thread_count));
    std::latch start_gate(1);
    std::latch finished(static_cast<std::ptrdiff_t>(thread_count));

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index)
    {
        threads.emplace_back([&, thread_index]()
        {
            ready.count_down();
            start_gate.wait();
            operation(thread_index, OperationsForThread(thread_index, thread_count));
            finished.count_down();
        });
    }

    ready.wait();
    const auto bengin = std::chrono::steady_clock::now();
    start_gate.count_down();
    finished.wait();
    const auto end = std::chrono::steady_clock::now();

    for (std::thread &thread: threads)
    {
        thread.join();
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - bengin);
}

std::chrono::nanoseconds BenchmarkMutex(std::size_t thread_count)
{
    g_Counter = 0;
    const auto elapsed = BenchmarkThreads(thread_count, [](std::size_t, uint64_t operations)
    {
        for (uint64_t index = 0; index < operations; ++index)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            ++g_Counter;
        }
    });
    return elapsed;
}

std::chrono::nanoseconds BenchmarkSharedMutex(std::size_t thread_count)
{
    g_Counter = 0;
    const auto elapsed = BenchmarkThreads(thread_count, [](std::size_t, uint64_t operations)
    {
        for (uint64_t index = 0; index < operations; ++index)
        {
            std::lock_guard<std::shared_mutex> lock(g_sharedMutex);
            ++g_Counter;
        }
    });
    return elapsed;
}

std::chrono::nanoseconds BenchmarkAtomic(std::size_t thread_count)
{
    g_atomicCounter.store(0, std::memory_order_relaxed);
    const auto elsapsed = BenchmarkThreads(thread_count, [](std::size_t, uint64_t operations)
    {
        for (uint64_t index = 0; index < operations; ++index)
        {
            g_atomicCounter.fetch_add(1, std::memory_order_relaxed);
        }
    });
    return elsapsed;
}

std::chrono::nanoseconds BenchmarkAtomicCas(std::size_t thread_count)
{
    g_atomicCounter.store(0, std::memory_order_relaxed);
    const auto elapsed = BenchmarkThreads(thread_count, [](std::size_t, uint64_t operations)
    {
        for (uint64_t index = 0; index < operations; ++index)
        {
            uint64_t expected = g_atomicCounter.load(std::memory_order_relaxed);
            while (!g_atomicCounter.compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed, std::memory_order_relaxed))
            {}
        }
    });
    return elapsed;
}


template<typename Benchmark>
double Run(Benchmark &&benchmark)
{
    long double total_nanoseconds = 0.0L;
    for (int run = 0; run < RUNS; ++run)
    {
        total_nanoseconds += static_cast<long double>(benchmark().count());
    }
    return static_cast<double>(total_nanoseconds / RUNS);
}


void PrintResult(std::string_view name, double total_nanoseconds)
{
    const double nanoseconds_per_operation = total_nanoseconds / static_cast<double>(g_target);
    std::cout << std::left << std::setw(30) << name << std::right
            << total_nanoseconds << " ns \ttotal, "
            << nanoseconds_per_operation << " ns/op\n";
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: ./test_lock count\n";
        return -1;
    }
    g_target = std::atoi(argv[1]);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "operations: " << g_target << ", average of " << RUNS << " runs\n";
    const std::vector<std::size_t> thread_counts = {1, 2, 4, 8, 10};
    for (const std::size_t thread_count: thread_counts)
    {
        std::cout << "threads: " << thread_count << "\n";
        PrintResult("mutex", Run([&]() { return BenchmarkMutex(thread_count); }));
        PrintResult("shared mutex", Run([&]() { return BenchmarkSharedMutex(thread_count); }));
        PrintResult("atomic", Run([&]() { return BenchmarkAtomic(thread_count); }));
        PrintResult("atomic CAS", Run([&]() { return BenchmarkAtomicCas(thread_count); }));
    }
}
