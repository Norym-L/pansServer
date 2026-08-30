#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN // 排除不常用Windowsapi
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX // 不要生成MAX和MIN 宏
#define NOMINMAX
#endif

#include <windows.h>
#endif

#include <pans/utils/thread_uitls.h>
#include <pans/utils/string_utils.h>
#include <pans/macros.h>

namespace pans
{
namespace
{
#if defined(_WIN32)
struct LocalFreeDeleter
{
    void operator()(wchar_t *value) const noexcept
    {
        if (value != nullptr)
        {
            (void) ::LocalFree(value);
        }
    }
};
#endif

[[nodiscard]] std::string GetNativeThreadName() noexcept
{
    try
    {
#if defined(__linux__)
        std::array<char, 16> native_name{};
        if (pthread_getname_np(pthread_self(), native_name.data(), native_name.size()) != 0)
        {
            return {};
        }
        return std::string(native_name.data());
#elif defined(_WIN32)
        PWSTR raw_name = nullptr;
        if (FAILED(::GetThreadDescription(::GetCurrentThread(), &raw_name)))
        {
            return {};
        }
        const std::unique_ptr<wchar_t, LocalFreeDeleter> native_name(raw_name);
        if (native_name == nullptr)
        {
            return {};
        }
        return StringUtils::WStringToString(std::string_view(native_name));
#else
        return {};
#endif
    }
    catch (...)
    {
        return {};
    }
}

void SetNativeThreadName(std::string_view name) noexcept
{
#if defined(__linux__)
    std::array<char, 16> native_name{};
    const std::size_t name_size = std::min(name.size(), native_name.size() - 1);
    std::memcpy(native_name.data(), name.data(), name_size);
    (void) pthread_setname_np(pthread_self(), native_name.data());
#elif defined(_WIN32)
    try
    {
        const std::wstring wid_name = StringUtils::StringToWString(name);
        if (!wid_name.empty())
        {
            (void) ::SetThreadDescription(::GetCurrentThread(), wid_name.c_str());
        }
    }
    catch (...)
    {}
#elif
    (void) name; // 忽略未使用的变量
#endif
}

constexpr std::string_view DEFAULT_THREAD_NAME = "UNKNOWN";
thread_local std::string t_thread_name = GetNativeThreadName();
}

u64 GetThreadId() noexcept
{
#if defined(__linux__)
    static thread_local const u64 THREAD_ID = static_cast<u64>(::syscall(SYS_gettid));
#elif defined(_WIN32)
    static thread_local const u64 THREAD_ID = static_cast<u64>(::GetCurrentThread());
#else
    static thread_local const u64 THREAD_ID = static_cast<u64>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
    return THREAD_ID;
}

void SetThreadName(std::string name)
{
    if (name.empty())
    {
        ASSERT_NOEFFECT2(false, "passed empty thread name");
        std::string native_name = GetNativeThreadName();
        t_thread_name = native_name.empty() ? std::string(DEFAULT_THREAD_NAME) : std::move(native_name);
        return;
    }
    t_thread_name = std::move(name);
    SetNativeThreadName(t_thread_name);
}

std::string_view GetThreadName() noexcept
{
    return t_thread_name;
}
}
