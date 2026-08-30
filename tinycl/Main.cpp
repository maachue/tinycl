#define NAOCHUE_TINYCL_USE_IOURING
#include "DumbDefer.h"
#include "Global.h"
#include "liburing.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <print>
#include <sys/poll.h>
#include <system_error>
#include <termios.h>
#include <unistd.h>

static struct termios OriginTerm;

constexpr size_t TerminalReplySize = 1024;

int DevTTY = -1;

void term_restore(std::error_code *Err) {
  if (tcsetattr(DevTTY, TCSANOW, &OriginTerm) != 0) {
    if (Err) {
      *Err = std::error_code{errno, std::generic_category()};
    }

    return;
  }
}

[[nodiscard]] bool term_enter(std::error_code &Err) {
  if (!isatty(DevTTY)) {
    Err = errorCodeFromERRNO();
    return false;
  }

  if (tcgetattr(DevTTY, &OriginTerm) != 0) {
    Err = errorCodeFromERRNO();
    return false;
  }

  auto Copy = OriginTerm;
  Copy.c_lflag &= ~(ECHO | ICANON);
  Copy.c_cc[VMIN] = 1;
  Copy.c_cc[VTIME] = 0;

  if (tcsetattr(DevTTY, TCSANOW, &Copy) != 0) {
    Err = errorCodeFromERRNO();
    return false;
  }

  return true;
}

inline void updateClock(bool UTC = false) {
  static auto UTCNow = std::chrono::system_clock::now();
  UTCNow = std::chrono::system_clock::now();
  if (UTC) {
    std::print(stdout, "{:%H:%M:%S}", UTCNow);
  } else {
    auto LocalTime =
        std::chrono::zoned_time{std::chrono::current_zone(), UTCNow};
    std::print(stdout, "{:%H:%M:%S}", LocalTime);
  }
}

// TODO: CLI Parser???
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main() {
  // open /dev/tty
  if (DevTTY = open("/dev/tty", O_CLOEXEC | O_RDWR); DevTTY == -1) {
    std::println(stderr,
                 NAOCHUE_TINYCL_BEGINERROR ": Failed to open /dev/tty: {}",
                 strerror(errno));
    return 1;
  }
  DumbDefer Cleanup_devtty([]() { close(DevTTY); });

  // option via environment variable. Why not?
  std::error_code Err;
  if (std::getenv("NAOCHUE_CLEAR_SCREEN")) {
    if (!writeAll(DevTTY, ClearScreen.data(), ClearScreen.size(), Err)) {
      std::println(stderr,
                   NAOCHUE_TINYCL_BEGINERROR
                   ": Failed to write seq clear screen: {}",
                   Err.message());
      return 1;
    }
  }

  // enter mode
  if (!term_enter(Err)) {
    std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": {}", Err.message());
    return 1;
  }
  DumbDefer Cleanup_terminal([]() { term_restore(nullptr); });

#ifdef NAOCHUE_TINYCL_USE_IOURING
  struct io_uring Ring;
  if (auto Err = io_uring_queue_init(4, &Ring, 0); Err < 0) {
    std::println(stderr,
                 NAOCHUE_TINYCL_BEGINERROR ": Failed to open /dev/tty: {}",
                 strerror(-Err));
    return 1;
  }
  DumbDefer Cleanup_Ring([&]() { io_uring_queue_exit(&Ring); });

  struct __kernel_timespec SpecTimer{.tv_sec = 0, .tv_nsec = 100000000};

  enum Tag : __u64 { Tag_Tick, Tag_Key };

  auto registerTimerAwait = [&]() -> bool {
    auto *Sqe = io_uring_get_sqe(&Ring);
    if (!Sqe) {
      return false;
    }

    io_uring_prep_timeout(Sqe, &SpecTimer, 0, 0);
    io_uring_sqe_set_data64(Sqe, Tag_Tick);

    return true;
  };

  auto *TerminalInput = static_cast<char *>(malloc(TerminalReplySize));
  if (!TerminalInput) {
    std::println(stderr, NAOCHUE_TINYCL_BEGINERROR
                 ": Failed to allocate terminal input buffer");
    return 1;
  }
  DumbDefer Cleanup_Buffer([&]() { free(TerminalInput); });

  auto registerKeyAwait = [&]() -> bool {
    auto *Sqe = io_uring_get_sqe(&Ring);
    if (!Sqe) {
      return false;
    }

    io_uring_prep_read(Sqe, DevTTY, TerminalInput, TerminalReplySize, 0);
    io_uring_sqe_set_data64(Sqe, Tag_Key);

    return true;
  };

  if (!registerKeyAwait() || !registerTimerAwait()) {
    std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": Out of SQE");
    return 1;
  }

  if (auto EInt = io_uring_submit(&Ring); EInt < 0) {
    std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": {}", strerror(-EInt));
    return 1;
  } else if (EInt != 2) {
    std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": Missing SQE");
    return 1;
  }

  bool Running = true;
  while (Running) {
    io_uring_cqe *Cqe;
    if (auto EIn = io_uring_wait_cqe(&Ring, &Cqe); EIn < 0) {
      std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": {}", strerror(-EIn));
      return 1;
    }

    if (Cqe->res < 0 && Cqe->res != -ETIME) {
      std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": {}",
                   strerror(-Cqe->res));
      return 1;
    }

    auto What = static_cast<Tag>(io_uring_cqe_get_data64(Cqe));
    auto DataOfTrans = Cqe->res;

    io_uring_cqe_seen(&Ring, Cqe);

    switch (What) {
    case Tag_Key: {
      if (DataOfTrans > 0) {
        Running = false;
      }
      break;
    }
    case Tag_Tick:
      updateClock();
      std::print("\r");
      std::fflush(stdout);
      if (!registerTimerAwait()) {
        std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": Out of SQE");
        return 1;
      }
      break;
    }

    if (auto EInt = io_uring_submit(&Ring); EInt < 0) {
      std::println(stderr, NAOCHUE_TINYCL_BEGINERROR ": {}", strerror(-EInt));
      return 1;
    }
  }
#endif

  std::print("\n");

  return 0;
}
