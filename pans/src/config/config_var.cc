#include <atomic>
#include <stdexcept>
#include <string>
#include <string_view>

#include <pans/config/config_var.h>

namespace pans
{
namespace detail
{
namespace
{
constexpr std::string_view CONFIG_NAME_CHARSET = "abcdefghijklmnopqrstuvwxyz._1234567890"; // 所有合法字符
}

ListenerId NetxListenerId() noexcept
{
    static std::atomic<ListenerId> s_next{1};
    return s_next.fetch_add(1, std::memory_order_relaxed);
}

std::string NormalizeConfigName(std::string name)
{
    for (char& ch: name)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return name;
}

bool IsValidConfigName(const std::string_view name) noexcept
{
    return !name.empty() && name.find_first_not_of(CONFIG_NAME_CHARSET) == std::string_view::npos;
}
}

ConfigVarBase::ConfigVarBase(std::string name, std::string description)
    : m_name(detail::NormalizeConfigName(std::move(name)))
    , m_description(std::move(description))
{
    if (!detail::IsValidConfigName(m_name))
    {
        detail::ReportConfigError("ConfigVarBase::ConfigVarBase", m_name, {}, "配置包含非法字符");
        throw std::invalid_argument("pans: invalid config name: " + m_name);
    }
}

ConfigVarBase::~ConfigVarBase() = default;

std::string ConfigVarBase::toString() const
{
    try
    {
        const YAML::Node node = toNode();
        if (node.IsScalar())
        {
            return node.Scalar();
        }
        YAML::Emitter emitter;
        emitter << node;
        if (!emitter.good())
        {
            detail::ReportConfigError("ConfigVarBase::toString", m_name, getTypeName(), emitter.GetLastError());
            return {};
        }
        return emitter.c_str();
    }
    catch (const std::exception& e)
    {
        detail::ReportConfigError("ConfigVarBase::toString", m_name, getTypeName(), e.what());
    }
    catch (...)
    {
        detail::ReportConfigError("ConfigVarBase::toString", m_name, getTypeName(), "未知异常");
    }
    return {};
}

bool ConfigVarBase::fromString(std::string_view value)
{
    try
    {
        return fromNode(YAML::Load(std::string(value)));
    }
    catch (const std::exception& e)
    {
        detail::ReportConfigError("ConfigVarBase::fromString", m_name, getTypeName(), e.what());
    }
    catch (...)
    {
        detail::ReportConfigError("ConfigVarBase::fromString", m_name, getTypeName(), "未知异常");
    }
    return false;
}
}
