#ifndef PANS_INCLUDE_PANS_HOOK_H
#define PANS_INCLUDE_PANS_HOOK_H

#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <pans/export.h>
#include <pans/types.h>

namespace pans
{
[[nodiscard]] PANS_API bool IsHookEnable() noexcept;

PANS_API void SetHookEnable(bool enabled) noexcept;
} // namesapce pans

extern "C"
{
using sleep_fun = unsigned int (*)(unsigned int);
extern PANS_API sleep_fun sleep_f;

using usleep_fun = int (*)(useconds_t);
extern PANS_API usleep_fun usleep_f;

using nanosleep_fun = int (*)(const struct timespec*, struct timespec*);
extern PANS_API nanosleep_fun nanosleep_f;

using socket_fun = int (*)(int, int, int);
extern PANS_API socket_fun socket_f;

using socketpair_fun = int (*)(int, int, int, int[2]);
extern PANS_API socketpair_fun socketpair_f;

using connect_fun = int (*)(int, const struct sockaddr*, socklen_t);
extern PANS_API connect_fun connect_f;

using accept_fun = int (*)(int, struct sockaddr*, socklen_t*);
extern PANS_API accept_fun accept_f;

using read_fun = ssize_t (*)(int, void*, size_t);
extern PANS_API read_fun read_f;

using readv_fun = ssize_t (*)(int, const struct iovec*, int);
extern PANS_API readv_fun readv_f;

using recv_fun = ssize_t (*)(int, void*, size_t, int);
extern PANS_API recv_fun recv_f;

using recvfrom_fun = ssize_t (*)(int, void*, size_t, int, struct sockaddr*, socklen_t*);
extern PANS_API recvfrom_fun recvfrom_f;

using recvmsg_fun = ssize_t (*)(int, struct msghdr*, int);
extern PANS_API recvmsg_fun recvmsg_f;

using recvmmsg_fun = int (*)(int, struct mmsghdr*, unsigned int, int, int, struct timespec*);
extern PANS_API recvmmsg_fun recvmmsg_f;

using write_fun = ssize_t (*)(int, const void*, size_t);
extern PANS_API write_fun write_f;

using writev_fun = ssize_t (*)(int, const struct iovec*, int);
extern PANS_API writev_fun writev_f;

using send_fun = ssize_t (*)(int, const void*, size_t, int);
extern PANS_API send_fun send_f;

using sendto_fun = ssize_t (*)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
extern PANS_API sendto_fun sendto_f;

using sendmsg_fun = ssize_t (*)(int, const struct msghdr*, int);
extern PANS_API sendmsg_fun sendmsg_f;

using close_fun = int (*)(int);
extern PANS_API close_fun close_f;

using fcntl_fun = int (*)(int, int, ...);
extern PANS_API fcntl_fun fcntl_f;

using ioctl_fun = int (*)(int, unsigned long, ...);
extern PANS_API ioctl_fun ioctl_f;

using getsockopt_fun = int (*)(int, int, int, void*, socklen_t*);
extern PANS_API getsockopt_fun getsockopt_f;

using setsockopt_fun = int (*)(int, int, int, const void*, socklen_t);
extern PANS_API setsockopt_fun setsockopt_f;

PANS_API int connect_with_timeout(int fd, const struct sockaddr* address, socklen_t address_length, u64 timeout_ms);
} // extern "C"

#endif // PANS_INCLUDE_PANS_HOOK_H
