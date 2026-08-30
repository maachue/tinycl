#ifndef NAOCHUE_TINYCL_DUMBDEFER_H
#define NAOCHUE_TINYCL_DUMBDEFER_H

#include <concepts>
#include <type_traits>
#include <utility>

template <typename T>
  requires std::invocable<T> && std::same_as<std::invoke_result_t<T>, void>
class DumbDefer {
public:
  explicit DumbDefer(T &&Fn) : Fn(std::forward<T>(Fn)) {}
  ~DumbDefer() { Fn(); }

  // no copy
  DumbDefer(const DumbDefer &) = delete;
  template <typename O> DumbDefer(const DumbDefer<O> &) = delete;

  // no move
  DumbDefer(DumbDefer &&) = delete;
  template <typename O> DumbDefer(DumbDefer<O> &&) = delete;

private:
  std::decay_t<T> Fn;
};

template <typename T>
  requires std::invocable<T>
DumbDefer(T) -> DumbDefer<T>;

#endif
