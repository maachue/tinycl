#include "liburing.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <print>
#include <string>
#include <system_error>
#include <termios.h>

template <typename T> struct DumbDefer {
  T Calle;

  DumbDefer(T &&Calle) : Calle(Calle) {}
  ~DumbDefer() { Calle(); }
};

template <typename T> DumbDefer(T) -> DumbDefer<T>;

constexpr long long NANOSEC_UPDATE_CLOCK = 100000000ULL;

enum TagTag { TAGTAG_TIMEOUT = 1, TAGTAG_DEVTTY = 2 };

static struct termios S;

std::error_code Restore(int DEVTTY) {
  if (tcsetattr(DEVTTY, TCSANOW, &S) < 0) {
    return std::error_code{errno, std::generic_category()};
  }

  return {};
}

[[nodiscard]] std::error_code Enter(int DEVTTY) {
  if (!isatty(DEVTTY)) {
    return std::error_code{errno, std::generic_category()};
  }

  if (tcgetattr(DEVTTY, &S) < 0) {
    return std::error_code{errno, std::generic_category()};
  }

  struct termios Copy = S;

  Copy.c_lflag &= ~(ECHO | ICANON);
  Copy.c_cc[VMIN] = 0;
  Copy.c_cc[VTIME] = 0;

  if (tcsetattr(DEVTTY, TCSANOW, &Copy) < 0) {
    return std::error_code{errno, std::generic_category()};
  }

  return {};
}

int main(int argc, char **argv) {
  constexpr std::string_view BeginError = "tincl: Error";
  auto Now = std::chrono::system_clock::now();
  std::string ClockStr = std::format("\r{:%H:%M:%S}", Now);

  auto DEVTTY = open("/dev/tty", O_WRONLY | O_NONBLOCK);
  if (DEVTTY < 0) {
    std::println(stderr, "{}: {}", BeginError, strerror(errno));
    return 1;
  }
  DumbDefer CleanupDEVTTY_{[&]() { close(DEVTTY); }};

  if (auto E = Enter(DEVTTY)) {
    std::println(stderr, "{}: {}", BeginError, E.message());
    return 1;
  }
  DumbDefer CleanupTerminal_{[&]() {
    auto E = Restore(DEVTTY);
    (void)E;
  }};

  struct io_uring Ring;
  auto Err = io_uring_queue_init(8, &Ring, 0);
  if (Err != 0) {
    std::println(stderr, "{}: {}", BeginError, strerror(-Err));
    return 1;
  }
  DumbDefer CleanupRing_{[&]() { io_uring_queue_exit(&Ring); }};

  struct __kernel_timespec Ts{.tv_sec = 1, .tv_nsec = 0};

  auto registerTimeout = [&]() -> int {
    auto *Sqe = io_uring_get_sqe(&Ring);
    if (!Sqe) {
      std::println(stderr, "{}: IO_URING: SQE NULL", BeginError);
      return 1;
    }

    // timeout
    io_uring_prep_timeout(Sqe, &Ts, 0, 0);
    io_uring_sqe_set_data(Sqe, (void *)TAGTAG_TIMEOUT);

    return 0;
  };

  auto registerPoll = [&]() {
    auto *Sqe = io_uring_get_sqe(&Ring);
    if (!Sqe) {
      std::println(stderr, "{}: IO_URING: SQE NULL", BeginError);
      return 1;
    }

    // poll /dev/tty
    io_uring_prep_poll_add(Sqe, DEVTTY, POLLIN);
    io_uring_sqe_set_data(Sqe, (void *)TAGTAG_DEVTTY);

    return 0;
  };

  auto Fail = registerTimeout();
  if (Fail != 0) {
    return Fail;
  }

  Fail = registerPoll();
  if (Fail != 0) {
    return Fail;
  }

  io_uring_submit(&Ring);

  bool Fine = true;
  while (Fine) {
    struct io_uring_cqe *Cqe;
    Err = io_uring_wait_cqe(&Ring, &Cqe);
    if (Err != 0) {
      std::println(stderr, "{}: {}", BeginError, strerror(-Err));
      return 1;
    }

    if (Cqe->res < 0 && !(Cqe->res & POLLIN) && Cqe->res != -ETIME) {
      std::println(stderr, "{}: {}", BeginError, strerror(-Cqe->res));
      return 1;
    }

    switch (static_cast<TagTag>(
        reinterpret_cast<uintptr_t>(io_uring_cqe_get_data(Cqe)))) {
    case TAGTAG_DEVTTY: {
      char buf[1024];
      while (read(DEVTTY, buf, sizeof(buf)) > 0)
        ;

      Fine = false;
      io_uring_cqe_seen(&Ring, Cqe);
      break;
    }
    case TAGTAG_TIMEOUT: {
      io_uring_cqe_seen(&Ring, Cqe);

      Now = std::chrono::system_clock::now();
      ClockStr = std::format("\r{:%H:%M:%S}", Now);

      auto E = write(DEVTTY, ClockStr.data(), ClockStr.size());
      if (E < 0) {
        std::println(stderr, "{}: Write failed: {}", BeginError,
                     strerror(errno));
        return 1;
      }

      Fail = registerTimeout();
      if (Fail != 0)
        return 1;

      io_uring_submit(&Ring);
      break;
    }
    }
  }

  return 0;
}
