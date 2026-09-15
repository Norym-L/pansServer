#ifndef PANS_INCLUDE_PANS_LOG_LEVEL_H
#define PANS_INCLUDE_PANS_LOG_LEVEL_H

#include <string_view>

#include <pans/export.h>
#include <pans/types.h>

namespace pans
{
class PANS_API LogLevel final
{
public:
    enum class Level : u8
    {
        LOG_LV_DEBUG = 1, // 记录调试细节，如变量值、程序执行流程
        LOG_LV_INFO  = 2, // 记录正常运行的信息，如服务启动、数据库连接成功、玩家连接成功等
        LOG_LV_WARN  = 3, // 可能存在问题，但程序还能继续执行，需要检查处理
        LOG_LV_ERROR = 4, // 操作失败，或功能异常，需要检查处理
        LOG_LV_FATAL = 5, // 严重故障，程序继续执行可能存在较大问题
        LOG_LV_OFF   = 6, // 关闭日志
    };

    [[nodiscard]] static std::string_view ToString(Level level) noexcept;
    [[nodiscard]] static LogLevel::Level FromString(std::string_view value) noexcept;
};
}

#endif //PANS_INCLUDE_PANS_LOG_LEVEL_H
