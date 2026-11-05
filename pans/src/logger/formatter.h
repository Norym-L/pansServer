#ifndef PANS_SRC_LOGGER_FORMATTER_H
#define PANS_SRC_LOGGER_FORMATTER_H

#include <memory>
#include <string>
#include <vector>

#include "logger/buffer.h"
#include "logger/buffer_config.h"
#include "logger/log_record.h"

namespace pans::detail
{
using FormattedRecordBuffer = InlineBuffer<FORMATTED_RECORD_INLINE_CAPACITY>;

class Formatter final
{
public:
    class FormatItem
    {
    public:
        virtual ~FormatItem() = default;

        virtual void format(const LogRecordView& record, FormattedRecordBuffer& output) const = 0;
    };

    explicit Formatter(std::string_view pattern);

    void format(const LogRecordView& record, FormattedRecordBuffer& output) const;

    [[nodiscard]] const std::string& getPatern() const noexcept { return m_patern; }

private:
    int parse();

    void addLiteral(std::string& literal);

private:
    std::string m_patern = "";
    std::vector<std::unique_ptr<FormatItem> > m_items;
};
} //  pans::detail

#endif //PANS_SRC_LOGGER_FORMATTER_H
