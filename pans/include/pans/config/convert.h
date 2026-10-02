#ifndef PANS_INCLUDE_PANS_CONVERT_H
#define PANS_INCLUDE_PANS_CONVERT_H

#include <charconv>
#include <concepts>
#include <cstddef>
#include <list>
#include <map>
#include <set>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

#include <pans/type_name.h>
#include <pans/utils/string_utils.h>

namespace pans
{
namespace detail
{
// [[noreturn]] 标记表示函数执行后不会正常返回，编译器知道调用后代码不可达，可以省掉一些死代码、寄存器保存等
/**
 * 抛出转换错误
 * @param type_name 类型名称
 * @param reson 原因
 * @param value 值
 */
[[noreturn]] PANS_API void ThrowConvertError(std::string_view type_name, std::string_view reson, std::string_view value = {});
// 转换到bool值 (yes no、true false、on off...)
[[nodiscard]] PANS_API bool ParseBool(std::string_view text, bool& out) noexcept;

[[nodiscard]] PANS_API const std::string& ScalarText(const YAML::Node& node, std::string_view type_name);
// 转换到数值类型 (int、float、double、long double...)
template<typename T>
    requires std::is_arithmetic_v<T> && (!std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] T ParseArithmetic(const std::string_view text, const std::string_view type_name)
{
    std::string_view body = StringUtils::Trim(text);
    if (body.empty())
    {
        ThrowConvertError(type_name, "空标量无法转化为实值");
    }

    if (body.front() == '+')
    {
        body.remove_prefix(1);
    }

    T value{};
    std::from_chars_result result{};
    if constexpr (std::is_integral_v<T>)
    {
        int base = 10; // 默认十进制
        if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
        {
            base = 16;
            body.remove_prefix(2); // 移除前缀 0x 或 0X
        }
        result = std::from_chars(body.data(), body.data() + body.size(), value, base);
    }
    else
    {
        result = std::from_chars(body.data(), body.data() + body.size(), value);
    }
    if (result.ec != std::errc{} || result.ptr != body.data() + body.size()) // 如果转换失败或未完全转换，则抛出错误
    {
        ThrowConvertError(type_name, "无法将标量转换为数值");
    }
    return value;
}

// 将数值类型转换为 YAML::Node
template<typename T>
    requires std::is_arithmetic_v<T> && (!std::is_same_v<std::remove_cv_t<T>, bool>)
[[nodiscard]] YAML::Node ArithmeticToNode(const T& value)
{
    char buffer[64]{};
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
    {
        ThrowConvertError(TypeName<T>(), "无法将数值类型转化为字符串");
    }
    return YAML::Node(std::string(buffer, static_cast<std::size_t>(result.ptr - buffer)));
}
}
}

#endif //PANS_INCLUDE_PANS_CONVERT_H
