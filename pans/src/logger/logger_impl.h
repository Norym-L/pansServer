#ifndef PANS_SRC_LOGGER_LOGGER_IMPL_H
#define PANS_SRC_LOGGER_LOGGER_IMPL_H

#include <atomic>
#include <shared_mutex>
#include <string>
#include <vector>

#include <pans/logger/logger.h>

#include "formatter.h"
#include "log_record.h"
#include "pans/logger/log_level.h"

namespace pans
{
class Logger::Impl
{
public:
    explicit Impl(std::string name);
    [[nodiscard]] bool shouldLog(LogLevel::Level level) const noexcept;
    void submit(const detail::LogRecordView& record) noexcept;
    void setLevel(LogLevel::Level level) noexcept;
    [[nodiscard]] LogLevel::Level getLevel() const noexcept;
    [[nodiscard]] std::string_view getName() const noexcept;
    void setFormatter(std::shared_ptr<const detail::Formatter> formatter);
    [[nodiscard]] std::shared_ptr<const detail::Formatter> getFormatter() const noexcept;

    void addAppender(AppenderPtr appender);
    void removeAppender(const AppenderPtr& appender);
    void clearAppenders();

    void flush();
    void sync();
    void setRoot(LoggerPtr root) noexcept;

private:
    std::string m_name;
    std::atomic<LogLevel::Level> m_level{LogLevel::Level::LOG_LV_DEBUG};

    mutable std::shared_mutex m_mutex;
    std::shared_ptr<const detail::Formatter> m_formatter;
    std::vector<AppenderPtr> m_appenders;
    LoggerPtr m_root;
};

namespace detail
{
class LoggerAccess final
{
public:
    static void Submit(Logger& logger, const LogRecordView& record) noexcept;
    static void SetRoot(Logger& logger, LoggerPtr root) noexcept;
};
}
}


#endif // PANS_SRC_LOGGER_LOGGER_IMPL_H
