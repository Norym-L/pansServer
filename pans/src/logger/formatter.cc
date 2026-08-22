#include "formatter.h"
#include <unordered_map>

namespace pans::detail
{
constexpr std::string_view DEFAULT_DATE_FORMAT = "%Y-%m-%d %H:%M:%S";

Formatter::Formatter(std::string_view pattern)
    : m_patern(pattern)
{
    if (parse())
    {
        throw std::invalid_argument("invalid log format, parse failed.");
    }
}

void Formatter::format(const LogRecordView &record, FormattedRecordBuffer &output) const
{
    for (const auto &item: m_items)
    {
        item->format(record, output);
    }
}

template<typename T>
void AppendInteger(FormattedRecordBuffer &output, T value)
{
    std::array<char, 24> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    ASSERT_RETNONE2(result.ec == std::errc(), "trans " << value << " to chars failed.");
    output.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

class LiteralFormatItem final : public Formatter::FormatItem
{
public:
    explicit LiteralFormatItem(std::string value)
        : m_value(std::move(value))
    {}

    void format(const LogRecordView &, FormattedRecordBuffer &output) const override
    {
        output.append(m_value);
    }

private:
    std::string m_value;
};

class MessageFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        output.append(record.m_message);
    }
};

class LevelFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        output.append(LogLevel::ToString(record.m_level));
    }
};

class ElampseFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        AppendInteger(output, std::chrono::duration_cast<std::chrono::milliseconds>(record.m_elapsed).count());
    }
};

class LoggerNameFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        output.append(record.m_loggerName);
    }
};

class ThreadIdFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        AppendInteger(output, record.m_threadId);
    }
};

class NewLineFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &, FormattedRecordBuffer &output) const override
    {
        output.append("\n");
    }
};

class DateTimeFormatItem final : public Formatter::FormatItem
{
public:
    explicit DateTimeFormatItem(std::string_view format)
        : m_format(format.empty() ? DEFAULT_DATE_FORMAT : format)
    {}

    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        static thread_local time_t last_second = 0;
        static thread_local char cached_date_time[20] = {'\0'};
        const auto duration = record.m_timestamp.time_since_epoch();
        const time_t current_second = static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(duration).
            count());
        if (current_second != last_second)
        {
            std::tm buffer{};
#if defined(_WIN32)
            const error_t result = localtime_s(&buffer, &current_second);
            ASSERT_RETNONE2(result == 0, "failed to convert log time to local time.");
#else
            const std::tm* result = localtime_r(&current_second, &buffer);
            ASSERT_RETNONE2(result != nullptr, "failed to convert log time to local time.");
#endif
            const std::size_t size = std::strftime(cached_date_time, sizeof(cached_date_time), m_format.c_str(),
                                                   &buffer);
            ASSERT_RETNONE2(size != 0, "failed to format log time.");
            last_second = current_second;
        }
        output.append(cached_date_time);
    }

private:
    std::string m_format;
};

class MicrosecondsFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        const auto total_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
            record.m_timestamp.time_since_epoch()).count();
        s64 microseseconds = total_microseconds % 1000000;
        if (microseseconds < 0)
        {
            microseseconds += 1000000;
        }
        // 小数对齐补零
        std::array<char, MICROSECONDS_WIDTH> digits{};
        auto remaining = static_cast<u32>(microseseconds);
        for (std::size_t index = digits.size(); index-- > 0;)
        {
            digits[index] = static_cast<char>('0' + remaining % 10);
            remaining /= 10;
        }
        output.append(digits.data(), digits.size());
    }

private:
    static constexpr std::size_t MICROSECONDS_WIDTH = 6;
};

class FileNameFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        output.append(record.m_fileName);
    }
};

class LineFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        AppendInteger(output, record.m_line);
    }
};

class TabFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &, FormattedRecordBuffer &output) const override
    {
        output.append('\t');
    }
};

class FiberIdFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        AppendInteger(output, record.m_fiberId);
    }
};

class ThreadNameFormatItem final : public Formatter::FormatItem
{
public:
    void format(const LogRecordView &record, FormattedRecordBuffer &output) const override
    {
        output.append(record.m_threadName);
    }
};

using FormatItemFactory = std::unique_ptr<Formatter::FormatItem> (*)(std::string_view);

template<typename Item>
[[nodiscard]] std::unique_ptr<Formatter::FormatItem> CreateSimpleFormatItem(std::string_view)
{
    return std::make_unique<Item>();
}

template<typename Item>
[[nodiscard]] std::unique_ptr<Formatter::FormatItem> CreateConfiguredFormatItem(std::string_view format)
{
    return std::make_unique<Item>(format);
}

[[nodiscard]] const std::unordered_map<char, FormatItemFactory> &GetFormatItemFactories()
{
    static const std::unordered_map<char, FormatItemFactory> FORMAT_ITEM_FACTORIES{
        {'m', &CreateSimpleFormatItem<MessageFormatItem>},      // 程序员写日志时插入的message
        {'p', &CreateSimpleFormatItem<LevelFormatItem>},        // 日志等级
        {'r', &CreateSimpleFormatItem<ElampseFormatItem>},      // 程序启动到现在的时间
        {'c', &CreateSimpleFormatItem<LoggerNameFormatItem>},   // 日志器名称
        {'t', &CreateSimpleFormatItem<ThreadIdFormatItem>},     // 线程id
        {'n', &CreateSimpleFormatItem<NewLineFormatItem>},      // 换行
        {'d', &CreateConfiguredFormatItem<DateTimeFormatItem>}, // 当前时间
        {'u', &CreateSimpleFormatItem<FileNameFormatItem>},     // 文件名
        {'l', &CreateSimpleFormatItem<LineFormatItem>},         // 行号
        {'T', &CreateSimpleFormatItem<TabFormatItem>},          // 制表符
        {'F', &CreateSimpleFormatItem<FiberIdFormatItem>},      // 协程id
        {'N', &CreateSimpleFormatItem<ThreadNameFormatItem>},   // 线程名称
        };
    return FORMAT_ITEM_FACTORIES;
}

[[nodiscard]] std::unique_ptr<Formatter::FormatItem> CreateFormatItem(char directive, std::string_view format)
{
    const auto& factories = GetFormatItemFactories();
    const auto it = factories.find(directive);
    ASSERT_RETVAL2(it != factories.end(), nullptr, "unknow logger format directive: " << directive << ".");
    return it->second(format);
}

void Formatter::addLiteral(std::string& literal)
{
    if (literal.empty())
    {
        return;
    }
    m_items.push_back(std::make_unique<LiteralFormatItem>(std::move(literal)));
    literal.clear();
}

int Formatter::parse()
{
    std::string literal;
    for (std::size_t index = 0; index < m_patern.size(); ++index)
    {
        if (m_patern[index] != '%')
        {
            literal.push_back(m_patern[index]);
            continue;
        }
        ASSERT_RETVAL2(index + 1 <= m_patern.size(), -1, "logger format pattern ends with an incomplete directive.");
        const char directive = m_patern[++index];
        if (directive == '%')
        {
            literal.push_back('%');
            continue;
        }
        addLiteral(literal);
        std::string_view item_format;
        if (index + 1 < m_patern.size() && m_patern[index + 1] == '{')
        {
            const std::size_t closing_brace = m_patern.find('}', index + 2);
            ASSERT_RETVAL2(closing_brace != std::string::npos && closing_brace != (index + 2), -2, "missing a closing brace or empty.");
            item_format = std::string_view(m_patern).substr(index + 2, closing_brace - index - 2);
            index = closing_brace;
        }
        m_items.push_back(CreateFormatItem(directive, item_format));
    }
    addLiteral(literal);
    for (const auto& item:m_items)
    {
        ASSERT_RETVAL2(item != nullptr, -3, "logger formatter contains a null format item.");
    }
    return 0;
}
} //  pans::detail
