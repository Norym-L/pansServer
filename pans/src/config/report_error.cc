#include <stdexcept>
#include <string>
#include <string_view>

#include <pans/config/convert.h>

namespace pans::detail
{
void ThrowConvertError(const std::string_view type_name, const std::string_view reson, const std::string_view value)
{
    std::string message;
    message.reserve(type_name.size() + reson.size() + value.size() + 32);
    message.append("ConfigConvert<").append(type_name).append(">: ").append(reson);
    if (!value.empty())
    {
        message.append(", value=").append(value);
    }
    throw std::runtime_error(message);
}
}
