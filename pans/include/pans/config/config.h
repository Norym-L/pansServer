#ifndef PANS_INCLUDE_PANS_CONFIG_CONFIG_H
#define PANS_INCLUDE_PANS_CONFIG_CONFIG_H

#include <cstddef>
#include <functional>
#include <memory>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <yaml-cpp/yaml.h>

#include <pans/config/config_var.h>
#include <pans/config/convert.h>
#include <pans/export.h>
#include <pans/type_name.h>

namespace pans
{
class PANS_API Config final
{
public:
    using ConfigVarMap = std::unordered_map<std::string, ConfigVarBase::ptr>;
    using VisitFn = std::function<void(ConfigVarBase::ptr &)>;

    Config() = delete;

    // 查找配置，如果不存在就注册
    template<typename T>
    static std::shared_ptr<ConfigVar<T> > Lookup(std::string_view name, const T& default_value, std::string_view description = {})
    {
        const ConfigVarBase::ptr base = LookupOrCreate(
            name,
            TypeName<T>(),
            [&default_value, description](std::string normalized_name) -> ConfigVarBase::ptr
                {
                    return std::make_shared<ConfigVar<T> >(std::move(normalized_name), default_value, std::string(description));
                });
        if (!base)
        {
            return nullptr;
        }
        auto typed = std::dynamic_pointer_cast<ConfigVar<T> >(base);
        if (!typed)
        {
            detail::ReportConfigError("Config::Lookup", base->getName(), TypeName<T>(), "已注册的配置项使用了不同的转换器类型");
        }
        return typed;
    }

    [[nodiscard]] static ConfigVarBase::ptr LookupBase(std::string_view name);

    // 只查找不注册
    template<typename T>
    static std::shared_ptr<ConfigVar<T> > Lookup(const std::string_view name)
    {
        return std::dynamic_pointer_cast<ConfigVar<T> >(LookupBase(name));
    }

    static void LoadFromYaml(const YAML::Node& root);

    // 从目标目录读取yaml配置文件并记录修改时间，只读取发生变化的文件 force 是否忽略更改时间全量加载
    static void LoadFromConfDir(std::string_view path, bool force = false);

    static void Visit(const VisitFn& callback);

    static void ClearAllListeners();

    [[nodiscard]] static bool IsValidName(std::string_view name) noexcept;

    [[nodiscard]] static std::string NormalizeName(std::string_view name);

    [[nodiscard]] static std::size_t Size();

private:
    using CreateFn = std::function<ConfigVarBase::ptr(std::string)>;
    static ConfigVarBase::ptr LookupOrCreate(std::string_view name, std::string_view type_name, const CreateFn& create_fn);
    static ConfigVarBase::ptr CheckType(ConfigVarBase::ptr var, std::string_view type_name);
};
}


#endif //PANS_INCLUDE_PANS_CONFIG_CONFIG_H
