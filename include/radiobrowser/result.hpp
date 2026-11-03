// SPDX-License-Identifier: MIT
/// @file
/// A small value-or-error type modelled after C++23 std::expected.

#pragma once

#include <radiobrowser/error.hpp>

#include <cstdlib>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace radiobrowser {

/// Thrown when the value of a failed Result (or the error of a successful
/// Result) is accessed. Accessing the wrong alternative is a programming error;
/// check has_value() first. Builds without exception support abort instead.
class BadResultAccess : public std::exception {
 public:
  /// Creates the exception for a result that held the given error code.
  explicit BadResultAccess(ErrorCode code) noexcept : code_(code) {}

  /// Returns a generic description of the misuse.
  [[nodiscard]] const char* what() const noexcept override {
    return "radiobrowser::Result: accessed the alternative that is not held";
  }

  /// The error code held by the result, if it held an error.
  [[nodiscard]] ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

namespace detail {

[[noreturn]] inline void throw_bad_result_access(ErrorCode code) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
  throw BadResultAccess(code);
#else
  static_cast<void>(code);
  std::abort();
#endif
}

template <typename T>
using RemoveCvRef = std::remove_cv_t<std::remove_reference_t<T>>;

}  // namespace detail

/// Holds either a value of type T or an Error.
///
/// The interface is a subset of C++23 std::expected<T, Error> so that code can
/// migrate easily once C++23 is widely available. The library never throws;
/// only misuse of this type (accessing the alternative that is not held)
/// throws BadResultAccess.
template <typename T>
class [[nodiscard]] Result {
  static_assert(!std::is_reference_v<T>, "Result<T> does not support references");
  static_assert(!std::is_same_v<detail::RemoveCvRef<T>, Error>,
                "Result<Error> is ambiguous and not supported");

 public:
  /// The type of the value.
  using value_type = T;
  /// The type of the error.
  using error_type = Error;

  /// Constructs a successful result from anything T can be constructed from.
  template <typename U = T, std::enable_if_t<std::is_constructible_v<T, U&&> &&
                                                 !std::is_same_v<detail::RemoveCvRef<U>, Result> &&
                                                 !std::is_same_v<detail::RemoveCvRef<U>, Error>,
                                             int> = 0>
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  Result(U&& value) : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

  /// Constructs a failed result.
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

  /// True if the result holds a value.
  [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }

  /// True if the result holds a value.
  explicit operator bool() const noexcept { return has_value(); }

  /// Returns the value. Throws BadResultAccess if the result holds an error.
  [[nodiscard]] T& value() & { return *value_pointer(); }

  /// Returns the value. Throws BadResultAccess if the result holds an error.
  [[nodiscard]] const T& value() const& { return *value_pointer(); }

  /// Returns the value. Throws BadResultAccess if the result holds an error.
  [[nodiscard]] T&& value() && { return std::move(*value_pointer()); }

  /// Returns the error. Throws BadResultAccess if the result holds a value.
  [[nodiscard]] const Error& error() const& { return *error_pointer(); }

  /// Returns the error. Throws BadResultAccess if the result holds a value.
  [[nodiscard]] Error& error() & { return *error_pointer(); }

  /// Returns the error. Throws BadResultAccess if the result holds a value.
  [[nodiscard]] Error&& error() && { return std::move(*error_pointer()); }

  /// Returns the value or the given fallback if the result holds an error.
  template <typename U>
  [[nodiscard]] T value_or(U&& fallback) const& {
    if (const T* value = std::get_if<0>(&storage_)) {
      return *value;
    }
    return static_cast<T>(std::forward<U>(fallback));
  }

  /// Returns the value or the given fallback if the result holds an error.
  template <typename U>
  [[nodiscard]] T value_or(U&& fallback) && {
    if (T* value = std::get_if<0>(&storage_)) {
      return std::move(*value);
    }
    return static_cast<T>(std::forward<U>(fallback));
  }

  /// Checked access to the value, see value().
  [[nodiscard]] T* operator->() { return value_pointer(); }
  /// Checked access to the value, see value().
  [[nodiscard]] const T* operator->() const { return value_pointer(); }
  /// Checked access to the value, see value().
  [[nodiscard]] T& operator*() & { return *value_pointer(); }
  /// Checked access to the value, see value().
  [[nodiscard]] const T& operator*() const& { return *value_pointer(); }
  /// Checked access to the value, see value().
  [[nodiscard]] T&& operator*() && { return std::move(*value_pointer()); }

 private:
  // The pointer helpers never return null: they throw (or abort) instead.
  [[nodiscard]] T* value_pointer() {
    return const_cast<T*>(std::as_const(*this).value_pointer());  // NOLINT(*-const-cast)
  }

  [[nodiscard]] const T* value_pointer() const {
    if (const T* value = std::get_if<0>(&storage_)) {
      return value;
    }
    const Error* error = std::get_if<1>(&storage_);
    detail::throw_bad_result_access(error != nullptr ? error->code : ErrorCode::Internal);
  }

  [[nodiscard]] Error* error_pointer() {
    return const_cast<Error*>(std::as_const(*this).error_pointer());  // NOLINT(*-const-cast)
  }

  [[nodiscard]] const Error* error_pointer() const {
    if (const Error* error = std::get_if<1>(&storage_)) {
      return error;
    }
    detail::throw_bad_result_access(ErrorCode::Internal);
  }

  std::variant<T, Error> storage_;
};

}  // namespace radiobrowser
