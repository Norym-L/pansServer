#ifndef PANS_INCLUDE_PANS_UTILS_SYSTEM_UTILS_H
#define PANS_INCLUDE_PANS_UTILS_SYSTEM_UTILS_H

#include <chrono>

#include <pans/macros.h>
#include <pans/export.h>

namespace pans
{
[[nodiscard]] PANS_API std::chrono::steady_clock::duration GetElapsedTime() noexcept;
[[nodiscard]] PANS_API u64 GetFiberId() noexcept;
/**
 * 获取函数堆栈
 * @param size 层数
 * @param skip 跳过层数
 * @param prefix 添加返回堆栈的前缀
 * @return 堆栈
 */
[[nodiscard]] PANS_API std::string GetBacktrace(int size = 20, int skip = 1, const std::string& prefix = "");
}


#endif //PANS_INCLUDE_PANS_UTILS_SYSTEM_UTILS_H
