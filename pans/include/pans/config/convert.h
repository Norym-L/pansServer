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

template<typename T>
concept YamlDecodable = requires(const YAML::Node& node, T& out)
{
    { YAML::convert<T>::decode(node, out) } -> std::convertible_to<bool>;
};
template<typename T>
concept YamlEncodable = requires(const T& value)
{
    { YAML::convert<T>::encode(value) } -> std::convertible_to<YAML::Node>;
};
}

template<typename T>
class ConfigConvert
{
public:
    [[nodiscard]] static T FromNode(const YAML::Node& node)
    {
        constexpr std::string_view type_name = TypeName<T>();
        if constexpr (std::is_same_v<T, bool>)
        {
            bool value = false;
            const std::string& text = detail::ScalarText(node, type_name);
            if (!detail::ParseBool(text, value))
            {
                detail::ThrowConvertError(type_name, "无法解析为bool值", text);
            }
            return value;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return detail::ScalarText(node, type_name);
        }
        else if constexpr (std::is_arithmetic_v<T>)
        {
            return detail::ParseArithmetic<T>(detail::ScalarText(node, type_name), type_name);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            using Underlying = std::underlying_type_t<T>;
            return static_cast<T>(detail::ParseArithmetic<Underlying>(detail::ScalarText(node, type_name), type_name));
        }
        else
        {
            static_assert(detail::YamlDecodable<T>, "该类型需要特化 pans::ConfigConvert<T> 或 YMAL::convert<T>");
            T value{};
            if (!node.IsDefined() || !YAML::convert<T>::decode(node, value))
            {
                detail::ThrowConvertError(type_name, "YAML::convert 解码失败");
            }
            return value;
        }
    }

    [[nodiscard]] static YAML::Node ToNode(const T& value)
    {
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
        {
            return detail::ArithmeticToNode(value);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            return detail::ArithmeticToNode(static_cast<std::underlying_type_t<T>>(value));
        }
        else
        {
            static_assert(detail::YamlEncodable<T>, "该类型需要特化 pans::ConfigConvert<T> 或 YAML::convert<T>::encode");
            return YAML::Node(value);
        }
    }
};

namespace detail // 容器处理
{
template<typename C>
concept BackEmplaceable = requires(C& container, typename C::value_type&& value)
{
    container.emplace_back(std::move(value));
};
template<typename C>
concept Reservable = requires(C& container, std::size_t count)
{
    container.reserve(count);
};

template<typename Container>
class SequenceConvert // 线性容器
{
public:
    using value_type = typename Container::value_type;

    [[nodiscard]] static Container FromNode(const YAML::Node& node)
    {
        if (!node.IsSequence())
        {
            ThrowConvertError(TypeName<Container>(), "期望 YAML 序列节点");
        }

        Container container;
        if constexpr (Reservable<Container>)
        {
            container.reserve(node.size());
        }
        for (const YAML::Node& item: node)
        {
            if constexpr (BackEmplaceable<Container>)
            {
                container.emplace_back(ConfigConvert<value_type>::FromNode(item));
            }
            else
            {
                container.emplace(ConfigConvert<value_type>::FromNode(item));
            }
        }
        return container;
    }

    [[nodiscard]] static YAML::Node ToNode(const Container& container)
    {
        YAML::Node node(YAML::NodeType::Sequence);
        for (const value_type& item: container)
        {
            node.push_back(ConfigConvert<value_type>::ToNode(item));
        }
        return node;
    }
};

template<typename Map>
class StringMapConvert // 映射容器（映射到字符串）
{
public:
    using mapped_type = typename Map::mapped_type;

    [[nodiscard]] static Map FormMap(const YAML::Node& node)
    {
        if (!node.IsMap())
        {
            ThrowConvertError(TypeName<Map>(), "期望 YAML 映射型节点");
        }
        Map container;
        if constexpr (Reservable<Map>)
        {
            container.reserve(node.size());
        }
        for (const auto& entry: node)
        {
            if (!entry.first.IsScalar())
            {
                ThrowConvertError(TypeName<Map>(), "映射的键必须是标量");
            }
            container.emplace(entry.first.Scalar(), ConfigConvert<mapped_type>::FromNode(entry.second));
        }
        return container;
    }

    [[nodiscard]] static YAML::Node ToNode(const Map& container)
    {
        YAML::Node node(YAML::NodeType::Map);
        for (const auto& [key, value]: container)
        {
            node[key] = ConfigConvert<mapped_type>::ToNode(value);
        }
        return node;
    }
};
}

template<typename T, typename A>
class ConfigConvert<std::vector<T, A> > : public detail::SequenceConvert<std::vector<T, A> >
{};

template<typename T, typename A>
class ConfigConvert<std::list<T, A> > : public detail::SequenceConvert<std::list<T, A> >
{};

template<typename T, typename C, typename A>
class ConfigConvert<std::set<T, C, A> > : public detail::SequenceConvert<std::set<T, C, A> >
{};

template<typename T, typename H, typename E, typename A>
class ConfigConvert<std::unordered_set<T, H, E, A> > : public detail::SequenceConvert<std::unordered_set<T, H, E, A> >
{};

template<typename T, typename C, typename A>
class ConfigConvert<std::map<std::string, T, C, A> > : public detail::StringMapConvert<std::map<std::string, T, C, A> >
{};

template<typename T, typename H, typename E, typename A>
class ConfigConvert<std::unordered_map<std::string, T, H, E, A> > : public detail::StringMapConvert<std::unordered_map<std::string, T, H, E, A> >
{};
}

#endif //PANS_INCLUDE_PANS_CONVERT_H
