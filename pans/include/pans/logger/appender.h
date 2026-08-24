#ifndef PANS_INCLUDE_PANS_LOGGER_APPENDER_H
#define PANS_INCLUDE_PANS_LOGGER_APPENDER_H

#include <memory>
#include <string>

#include <pans/export.h>
#include <pans/logger/log_level.h>

namespace pans
{
namespace detail
{
class AppenderAccess;
}

class PANS_API Appender final
{
public:
    class Impl;
    ~Appender() = default;
    Appender(const Appender&) = delete;
    Appender& operator=(const Appender&) = delete;
    Appender(Appender&&) = delete;
    Appender& operator=(Appender&&) = delete;

    void setLevel(LogLevel::Level level) noexcept;
    [[nodiscard]] LogLevel::Level getLevel() const noexcept;

    void flush(); // 冲流缓冲区提交到脏页
    void sync(); // 从脏页提交到磁盘缓冲区
private:
    explicit Appender(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
    friend class detail::AppenderAccess;
};

using AppenderPtr = std::shared_ptr<Appender>;

[[nodiscard]] PANS_API AppenderPtr MakeStdoutAppender();
[[nodiscard]] PANS_API AppenderPtr MakeFileAppender(std::string file_name);

}

#endif //PANS_INCLUDE_PANS_LOGGER_APPENDER_H
