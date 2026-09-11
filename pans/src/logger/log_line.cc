#include <array>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <new>
#include <ostream>
#include <vector>

#include <pans/logger/log.h>
#include <pans/utils/system_utils.h>
#include <pans/utils/thread_uitls.h>

#include "buffer.h"
#include "buffer_config.h"
#include "logger_impl.h"
#include "log_record.h"
#include "pans/logger/log_level.h"

namespace pans::detail
{
struct LogLine::Impl final
{
    Impl(Logger &logger, LogLevel::Level level, u32 line, std::string_view file_name)
        : m_logger(logger)
        , m_level(level)
        , m_line(line)
        , m_fileName(file_name)
        , m_timestamp(std::chrono::system_clock::now())
        , m_elapsed(GetElapsedTime())
        , m_threadId(GetThreadId())
        , m_fiberId(GetFiberId())
        , m_threadName(GetThreadName())
        , m_streamBuffer(m_inlineBuffer)
        , m_stream(&m_streamBuffer)
    {}

    [[nodiscard]] LogRecordView getRecord() const noexcept
    {
        return {
            .m_level      = m_level,
            .m_loggerName = m_logger.getName(),
            .m_message    = m_inlineBuffer.view(),
            .m_timestamp  = m_timestamp,
            .m_elapsed    = m_elapsed,
            .m_threadId   = m_threadId,
            .m_fiberId    = m_fiberId,
            .m_threadName = m_threadName,
            .m_fileName   = m_fileName,
            .m_line       = m_line
        };
    }

    Logger &m_logger;
    LogLevel::Level m_level;
    u32 m_line = 0;
    std::string_view m_fileName;
    std::chrono::system_clock::time_point m_timestamp;
    std::chrono::steady_clock::duration m_elapsed;
    u64 m_threadId = 0;
    u64 m_fiberId = 0;
    std::string_view m_threadName;
    InlineBuffer<LOG_MESSAGE_INLINE_CAPACITY> m_inlineBuffer;
    SmallStreamBuffer<LOG_MESSAGE_INLINE_CAPACITY> m_streamBuffer; // 用于封装 m_inlineBuffer
    std::ostream m_stream;
};

LogLine::LogLine(Logger &logger, LogLevel::Level level, u32 line, std::string_view file_name)
{
    static_assert(sizeof(Impl) <= LOG_LINE_IMPL_SIZE, "LogLine inline implementation storage is to small");
    static_assert(alignof(Impl) <= alignof(std::max_align_t), "Never");
    std::construct_at(reinterpret_cast<Impl *>(m_implStorage), logger, level, line, file_name); // 在m_implStorage上构造Impl对象
}

LogLine::Impl &LogLine::getImpl() noexcept
{
    return *std::launder(reinterpret_cast<Impl *>(m_implStorage));
}

LogLine::~LogLine() noexcept
{
    Impl &impl = getImpl();
    LoggerAccess::Submit(impl.m_logger, impl.getRecord());
    std::destroy_at(&impl);
}

std::ostream &LogLine::stream() noexcept
{
    return getImpl().m_stream;
}

void LogPrintf(Logger &logger, LogLevel::Level level, u32 line, std::string_view file_name, const char *format, ...)
{
    LogLine log_line(logger, level, line, file_name); // 临时构建LogLine，随后析构submit
    if (format == nullptr)
    {
        log_line.stream() << "<null-format>";
        return;
    }
    std::array<char, PRINTF_FORMAT_INLINE_CAPACITY> inline_buffer;
    va_list arguments;
    va_list arguments_copy; // 可变参数存储在寄存器中，读取完会销毁寄存器，为了防止栈上存不下，所以需要copy一份
    va_start(arguments, format);
    va_copy(arguments_copy, arguments);
    const int required_size = std::vsnprintf(inline_buffer.data(), inline_buffer.size(), format, arguments);
    va_end(arguments);
    if (required_size < 0)
    {
        va_end(arguments_copy);
        log_line.stream() << "<format-error>";
        return;
    }
    const std::size_t message_size = static_cast<std::size_t>(required_size);
    if (message_size < inline_buffer.size())
    {
        va_end(arguments_copy);
        log_line.stream().write(inline_buffer.data(), static_cast<std::streamsize>(message_size));
        return;
    }
    std::vector<char> overflow_buffer(message_size + 1);
    const int second_result = std::vsnprintf(overflow_buffer.data(), overflow_buffer.size(), format, arguments_copy);
    va_end(arguments_copy);
    if (second_result < 0)
    {
        log_line.stream() << "<format-error>";
        return;
    }
    log_line.stream().write(overflow_buffer.data(), static_cast<std::streamsize>(message_size));
}
}
