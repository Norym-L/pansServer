#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>  // std::size_t
#include <cstdint>
#include <iomanip>  // setw
#include <iostream>
#include <sstream>  // std::stringstream
#include <string>
#include <string_view>

#include "logger/buffer.h"
#include "logger/buffer_config.h"

constexpr int RUNS = 7;
std::uint64_t g_target = 0;
std::atomic<std::uint64_t> BENCHMARK_SINK{0};

std::uint64_t Observe(std::string_view value) noexcept
{
    return static_cast<std::uint64_t>(value.size()) +
           static_cast<unsigned char>(value.front()) +
           static_cast<unsigned char>(value[value.size() / 2]) +
           static_cast<unsigned char>(value.back());
}

std::uint64_t WriteWithSmallStreamBuffer(std::string_view message)
{
    pans::detail::InlineBuffer<pans::detail::LOG_MESSAGE_INLINE_CAPACITY> buffer;
    pans::detail::SmallStreamBuffer<pans::detail::LOG_MESSAGE_INLINE_CAPACITY> stream_buffer(buffer);
    std::ostream stream(&stream_buffer);
    stream << message;
    return Observe(buffer.view());
}

std::uint64_t WriteWithStringStream(std::string_view message)
{
    std::stringstream stream;
    stream << message;
    return Observe(stream.view());
}

template<typename Operation>
std::chrono::nanoseconds BenchmarkWrites(Operation &&operation)
{
    std::uint64_t checksum = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (std::uint64_t iter = 0; iter < g_target; ++iter)
    {
        checksum += operation();
    }
    const auto end = std::chrono::steady_clock::now();
    BENCHMARK_SINK.fetch_xor(checksum, std::memory_order_relaxed);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
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
    std::cout << std::left << std::setw(30) << name << std::right
        << total_nanoseconds << " ns \ttotal, "
        << total_nanoseconds / static_cast<double>(g_target) << " ns/op\n";
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: ./test_log_buffer count\n";
        return 1;
    }

    g_target = std::stoull(argv[1]);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "operations: " << g_target << ", average of " << RUNS << " runs\n\n";
    constexpr std::array<std::size_t, 6> MESSAGE_SIZES = {20, 100, 200, 500, 1000, 2048};
    for (const auto message_size: MESSAGE_SIZES)
    {
        const std::string message(message_size, 'x');
        const double small_stream_buffer_nanoseconds = Run([&]()
            {
            return BenchmarkWrites([&]() { return WriteWithSmallStreamBuffer(message); });
            });
        const double stringstream_nanoseconds = Run([&]()
            {
            return BenchmarkWrites([&]() { return WriteWithStringStream(message); });
            });
        std::cout << "message: " << message_size << " characters" << std::endl;
        PrintResult("SmallStreamBuffer", small_stream_buffer_nanoseconds);
        PrintResult("Stringstream", stringstream_nanoseconds);
        std::cout << "speedup: " << stringstream_nanoseconds / small_stream_buffer_nanoseconds << "x\n\n";
    }
}
