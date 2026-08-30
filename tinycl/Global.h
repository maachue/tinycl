#ifndef NAOCHUE_TINYCL_GLOBAL_H
#define NAOCHUE_TINYCL_GLOBAL_H

#include <cerrno>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <unistd.h>

#define NAOCHUE_TINYCL_BEGINERROR "tinycl: Error"

constexpr std::string_view ClearScreen = "\x1b[2J\x1b[H";

extern int DevTTY;

inline std::error_code errorCodeFromERRNO() {
  return {errno, std::generic_category()};
}

inline bool writeAll(int Fd, const char *Bytes, size_t BytesSize,
                     std::error_code &Err) {
  while (auto N = write(Fd, Bytes, BytesSize)) {
    if (N == -1) {
      if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) {
        continue;
      }

      Err = errorCodeFromERRNO();
      return false;
    }

    if (N == 0) {
      break;
    }

    Bytes += N;
    BytesSize -= N;
  }

  return true;
}

#endif
