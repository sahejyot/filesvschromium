#include "base/logging/rust_log_integration.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace rust {
inline namespace cxxbridge1 {
// #include "rust/cxx.h"

namespace {
template <typename T>
class impl;
} // namespace

class String;

#ifndef CXXBRIDGE1_RUST_STR
#define CXXBRIDGE1_RUST_STR
class Str final {
public:
  Str() noexcept;
  Str(const String &) noexcept;
  Str(const std::string &);
  Str(const char *);
  Str(const char *, std::size_t);

  Str &operator=(const Str &) & noexcept = default;

  explicit operator std::string() const;

  const char *data() const noexcept;
  std::size_t size() const noexcept;
  std::size_t length() const noexcept;
  bool empty() const noexcept;

  Str(const Str &) noexcept = default;
  ~Str() noexcept = default;

  using iterator = const char *;
  using const_iterator = const char *;
  const_iterator begin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator cend() const noexcept;

  bool operator==(const Str &) const noexcept;
  bool operator!=(const Str &) const noexcept;
  bool operator<(const Str &) const noexcept;
  bool operator<=(const Str &) const noexcept;
  bool operator>(const Str &) const noexcept;
  bool operator>=(const Str &) const noexcept;

  void swap(Str &) noexcept;

private:
  class uninit;
  Str(uninit) noexcept;
  friend impl<Str>;

  std::array<std::uintptr_t, 2> repr;
};
#endif // CXXBRIDGE1_RUST_STR

#ifndef CXXBRIDGE1_RUST_OPAQUE
#define CXXBRIDGE1_RUST_OPAQUE
class Opaque {
public:
  Opaque() = delete;
  Opaque(const Opaque &) = delete;
  ~Opaque() = delete;
};
#endif // CXXBRIDGE1_RUST_OPAQUE

#ifndef CXXBRIDGE1_IS_COMPLETE
#define CXXBRIDGE1_IS_COMPLETE
namespace detail {
namespace {
template <typename T, typename = std::size_t>
struct is_complete : std::false_type {};
template <typename T>
struct is_complete<T, decltype(sizeof(T))> : std::true_type {};
} // namespace
} // namespace detail
#endif // CXXBRIDGE1_IS_COMPLETE

#ifndef CXXBRIDGE1_LAYOUT
#define CXXBRIDGE1_LAYOUT
class layout {
  template <typename T>
  friend std::size_t size_of();
  template <typename T>
  friend std::size_t align_of();
  template <typename T>
  static typename std::enable_if<std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_size_of() {
    return T::layout::size();
  }
  template <typename T>
  static typename std::enable_if<!std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_size_of() {
    return sizeof(T);
  }
  template <typename T>
  static
      typename std::enable_if<detail::is_complete<T>::value, std::size_t>::type
      size_of() {
    return do_size_of<T>();
  }
  template <typename T>
  static typename std::enable_if<std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_align_of() {
    return T::layout::align();
  }
  template <typename T>
  static typename std::enable_if<!std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_align_of() {
    return alignof(T);
  }
  template <typename T>
  static
      typename std::enable_if<detail::is_complete<T>::value, std::size_t>::type
      align_of() {
    return do_align_of<T>();
  }
};

template <typename T>
std::size_t size_of() {
  return layout::size_of<T>();
}

template <typename T>
std::size_t align_of() {
  return layout::align_of<T>();
}
#endif // CXXBRIDGE1_LAYOUT
} // namespace cxxbridge1
} // namespace rust

namespace logging {
  namespace internal {
    struct RustFmtArguments;
    using LogMessageRustWrapper = ::logging::internal::LogMessageRustWrapper;
  }
}

namespace logging {
namespace internal {
#ifndef CXXBRIDGE1_STRUCT_logging$internal$RustFmtArguments
#define CXXBRIDGE1_STRUCT_logging$internal$RustFmtArguments
struct RustFmtArguments final : public ::rust::Opaque {
  void format(::logging::internal::LogMessageRustWrapper &wrapper) const noexcept;
  ~RustFmtArguments() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_logging$internal$RustFmtArguments

extern "C" {
::std::size_t logging$internal$cxxbridge1$RustFmtArguments$operator$sizeof() noexcept;
::std::size_t logging$internal$cxxbridge1$RustFmtArguments$operator$alignof() noexcept;

void logging$internal$cxxbridge1$RustFmtArguments$format(::logging::internal::RustFmtArguments const &self, ::logging::internal::LogMessageRustWrapper &wrapper) noexcept;

void logging$internal$cxxbridge1$init_rust_log_crate() noexcept;

void logging$internal$cxxbridge1$LogMessageRustWrapper$write_to_stream(::logging::internal::LogMessageRustWrapper &self, ::rust::Str s) noexcept {
  void (::logging::internal::LogMessageRustWrapper::*write_to_stream$)(::rust::Str) = &::logging::internal::LogMessageRustWrapper::write_to_stream;
  (self.*write_to_stream$)(s);
}

void logging$internal$cxxbridge1$print_rust_log(::logging::internal::RustFmtArguments const &msg, char const *file, ::std::int32_t line, ::std::int32_t severity, bool verbose) noexcept {
  void (*print_rust_log$)(::logging::internal::RustFmtArguments const &, char const *, ::std::int32_t, ::std::int32_t, bool) = ::logging::internal::print_rust_log;
  print_rust_log$(msg, file, line, severity, verbose);
}
} // extern "C"

::std::size_t RustFmtArguments::layout::size() noexcept {
  return logging$internal$cxxbridge1$RustFmtArguments$operator$sizeof();
}

::std::size_t RustFmtArguments::layout::align() noexcept {
  return logging$internal$cxxbridge1$RustFmtArguments$operator$alignof();
}

void RustFmtArguments::format(::logging::internal::LogMessageRustWrapper &wrapper) const noexcept {
  logging$internal$cxxbridge1$RustFmtArguments$format(*this, wrapper);
}

void init_rust_log_crate() noexcept {
  logging$internal$cxxbridge1$init_rust_log_crate();
}
} // namespace internal
} // namespace logging
