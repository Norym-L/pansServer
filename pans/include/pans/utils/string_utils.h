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

    inline static constexpr std::string_view ASCII_WHITESPACE{" \t\n\r\f\v"};
    /**
     * 去除文本左右两端的非法字符（拥有字符数据）
     * @param text 文本
     * @param delimiter 非法字符
     * @return std::string
     */
    [[nodiscard]] static std::string Trim(const std::string& text, std::string_view delimiter = ASCII_WHITESPACE);
    /**
     * 去除文本左右两端的非法字符（不拥有字符数据）
     * @param text 文本
     * @param delimiter 非法字符
     * @return std::string_view 返回原文的视图
     */
    [[nodiscard]] static std::string_view Trim(std::string_view text, std::string_view delimiter = ASCII_WHITESPACE);
    /**
     * 去除文本左端的非法字符（拥有字符数据）
     * @param text 文本
     * @param delimiter 非法字符
     * @return std::string
     */
    [[nodiscard]] static std::string TrimLeft(const std::string& text, std::string_view delimiter = ASCII_WHITESPACE);
    /**
     * 去除文本左端的非法字符（不拥有字符数据）
     * @param text 文本
     * @param delimiter 非法字符
     * @return std::string_view 返回原文的视图
     */
    [[nodiscard]] static std::string_view TrimLeft(std::string_view text, std::string_view delimiter = ASCII_WHITESPACE);

    /**
     * 去除文本右端的非法字符（拥有字符数据）
     * @param text 文本
     * @param delimiter 非法字符
     * @return std::string
     */
    [[nodiscard]] static std::string TrimRight(const std::string& text, std::string_view delimiter = ASCII_WHITESPACE);
    /**
     * 去除文本右端的非法字符（不拥有字符数据）
     * @param text 文本
     * @param delimiter 非法字符
     * @return std::string_view 返回原文的视图
     */
    [[nodiscard]] static std::string_view TrimRight(std::string_view text, std::string_view delimiter = ASCII_WHITESPACE);
};
}


#endif //PANS_INCLUDE_PANS_UTILS_STRING_UTILS_H
