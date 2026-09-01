#ifndef PANS_INCLUDE_PANS_UTILS_THREAD_UITLS_H
#define PANS_INCLUDE_PANS_UTILS_THREAD_UITLS_H

#include <string>
#include <string_view>

#include <pans/export.h>
#include <pans/macros.h>

namespace pans
{
[[nodiscard]] PANS_API u64 GetThreadId() noexcept;

PANS_API void SetThreadName(std::string name);

[[nodiscard]] PANS_API std::string_view GetThreadName() noexcept;
}

#endif //PANS_INCLUDE_PANS_UTILS_THREAD_UITLS_H
