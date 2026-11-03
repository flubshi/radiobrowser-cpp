// SPDX-License-Identifier: MIT
/// @file
/// Cooperative cancellation of running calls.

#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace radiobrowser {

class CancellationSource;

/// A cheap, copyable handle that tells a running call whether it should stop.
///
/// A default constructed token is never cancelled. Tokens are thread-safe.
class CancellationToken {
 public:
  CancellationToken() = default;

  /// True once the owning CancellationSource has been cancelled.
  [[nodiscard]] bool is_cancelled() const noexcept {
    return state_ != nullptr && state_->load(std::memory_order_acquire);
  }

  /// True if this token can ever become cancelled.
  [[nodiscard]] bool can_be_cancelled() const noexcept { return state_ != nullptr; }

 private:
  friend class CancellationSource;

  explicit CancellationToken(std::shared_ptr<const std::atomic<bool>> state) noexcept
      : state_(std::move(state)) {}

  std::shared_ptr<const std::atomic<bool>> state_;
};

/// Creates tokens and cancels them.
///
/// Typical use: keep one source per component that issues calls (a window, a
/// session, a plugin instance), pass its token to every call and cancel it when
/// the component shuts down, so that blocked calls return promptly with
/// ErrorCode::Cancelled.
class CancellationSource {
 public:
  CancellationSource() : state_(std::make_shared<std::atomic<bool>>(false)) {}

  /// Requests cancellation of every call that uses a token of this source.
  void cancel() noexcept { state_->store(true, std::memory_order_release); }

  /// True once cancel() has been called.
  [[nodiscard]] bool is_cancelled() const noexcept {
    return state_->load(std::memory_order_acquire);
  }

  /// Returns a token bound to this source.
  [[nodiscard]] CancellationToken token() const noexcept { return CancellationToken(state_); }

 private:
  std::shared_ptr<std::atomic<bool>> state_;
};

}  // namespace radiobrowser
