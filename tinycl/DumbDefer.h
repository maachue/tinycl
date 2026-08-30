#ifndef NAOCHUE_TINYCL_DUMBDEFER_H
#define NAOCHUE_TINYCL_DUMBDEFER_H

#include <concepts>

template <typename T>
  requires std::invocable<T>
class DumbDefer {
public:
  explicit DumbDefer(T &&Fn) : Fn(Fn) {}
  ~DumbDefer() { Fn(); }

  // no copy
  DumbDefer(const DumbDefer &) = delete;
  template <typename O> DumbDefer(const DumbDefer<O> &) = delete;

  // no move
  DumbDefer(DumbDefer &&) = delete;
  template <typename O> DumbDefer(DumbDefer<O> &&) = delete;

private:
  T Fn;
};

template <typename T>
  requires std::invocable<T>
DumbDefer(T) -> DumbDefer<T>;

#endif
