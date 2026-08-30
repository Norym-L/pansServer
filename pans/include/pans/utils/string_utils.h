/**
 * @file string_utils.h
 * @brief string_utils 字符串工具，款字符 utf-8 字符转换
 *
 * @author norym
 * @date 2026/8/30
 */
#ifndef PANS_INCLUDE_PANS_UTILS_STRING_UTILS_H
#define PANS_INCLUDE_PANS_UTILS_STRING_UTILS_H

#include <cstring>
#include <cstdarg>
#include <string_view>

#include <pans/export.h>

namespace pans
{
class PANS_API StringUtils final
{
public:
    StringUtils() = delete;
    // 宽字符转为 utf-8
    [[nodiscard]] static std::string WStringToString(std::wstring_view text) noexcept;
    [[nodiscard]] static std::wstring StringToWString(std::string_view text) noexcept;
};
}


#endif //PANS_INCLUDE_PANS_UTILS_STRING_UTILS_H
