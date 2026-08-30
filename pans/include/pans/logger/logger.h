#ifndef PANS_INCLUDE_PANS_LOGGER_LOGGER_H
#define PANS_INCLUDE_PANS_LOGGER_LOGGER_H

#include <memory>
#include <string>
#include <string_view>

#include <pans/export.h>
#include <pans/logger/appender.h>
#include <pans/logger/log_level.h>

namespace pans
{
namespace detail
{
class LoggerAccess;
}

class PANS_API Logger final
{
public:
    class Impl;
    explicit Logger(std::string name = "root");
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    [[nodiscard]] bool shouldLog(LogLevel::Level level) const noexcept;

    void setLevel(LogLevel::Level level) noexcept;
    [[nodiscard]] LogLevel::Level getLevel() const noexcept;
    [[nodiscard]] std::string_view getName() const noexcept;
    void setFormatter(std::string_view pattern);
    [[nodiscard]] std::string getFormatterPattern() const;

    void addAppender(AppenderPtr appender);
    void removeAppender(const AppenderPtr& appender);
    void clearAppenders();

    void flush();
    void sync();

private:
    std::unique_ptr<Impl> m_impl;
    friend class detail::LoggerAccess;
};

using LoggerPtr = std::shared_ptr<Logger>;
[[nodiscard]] PANS_API LoggerPtr GetRootLogger();
[[nodiscard]] PANS_API LoggerPtr GetLogger(std::string_view name);

}

#endif //PANS_INCLUDE_PANS_LOGGER_LOGGER_H
