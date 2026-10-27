#ifndef PANS_INCLUDE_PANS_UTILS_FS_UTILS_H
#define PANS_INCLUDE_PANS_UTILS_FS_UTILS_H

#include <string>
#include <string_view>
#include <vector>

#include <pans/export.h>

namespace pans
{
class PANS_API FSUtils final
{
public:
    FSUtils() = delete;

    static void ListAllFile(std::vector<std::string>& files, std::string_view path, std::string_view suffix = {});
    [[nodiscard]] static bool MKdir(std::string_view dir_name) noexcept;
    [[nodiscard]] static bool IsRunningPidFile(std::string_view pid_file) noexcept;
    [[nodiscard]] static bool Unlink(std::string_view file_name, bool exist = false) noexcept;
};
}

#endif //PANS_INCLUDE_PANS_UTILS_FS_UTILS_H
