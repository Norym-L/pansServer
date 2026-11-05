#include <stdexcept>
#include <string>
#include <string_view>

#include <pans/config/config_var.h>
#include <pans/config/convert.h>
#include <pans/logger/log.h>

namespace pans::detail
{
namespace
{
LoggerPtr ConfigLogger()
{
    static LoggerPtr logger = GetLogger("system");
    return logger;
}
}

void ReportConfigError(std::string_view operation, std::string_view name, std::string_view type_name, std::string_view reason)
{
    const auto logger = ConfigLogger();
    PANS_LOG_ERROR(logger) << operation << "failed:"
        << " name=" << (name.empty() ? std::string_view("<none>") : name)
        << " type=" << (type_name.empty() ? std::string_view("<none>") : type_name)
        << " reason=" << reason;
}

void ThrowConvertError(const std::string_view type_name, const std::string_view reason, const std::string_view value)
{
    std::string message;
    message.reserve(type_name.size() + reason.size() + value.size() + 32);
    message.append("ConfigConvert<").append(type_name).append(">: ").append(reason);
    if (!value.empty())
    {
        message.append(", value=").append(value);
    }
    throw std::runtime_error(message);
}
}
