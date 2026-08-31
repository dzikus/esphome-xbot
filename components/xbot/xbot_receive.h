#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "xbot_protocol.h"

namespace esphome::xbot {

// Holds what has arrived until it makes whole frames. Notifications are not
// frame-aligned, so a reply can straddle two of them and a buffer can end mid
// frame. Storage is fixed.
template <size_t CAPACITY>
class RxAccumulator {
 public:
  void clear() { this->len_ = 0; }
  size_t size() const { return this->len_; }

  // Copies a payload in and de-obfuscates just that part; what was already
  // buffered is decoded. Returns how many buffered bytes had to be thrown away
  // to make room, which is only ever non-zero for a stream that never synced.
  size_t append(const uint8_t *data, size_t len, uint8_t key) {
    size_t dropped = 0;
    if (len > this->buf_.size() - this->len_) {
      dropped = this->len_;
      this->len_ = 0;
    }
    // A single payload larger than the whole buffer can never be held.
    if (len > this->buf_.size())
      return dropped;
    std::copy(data, data + len, this->buf_.begin() + static_cast<std::ptrdiff_t>(this->len_));
    apply_xor(std::span<uint8_t>(this->buf_).subspan(this->len_, len), key);
    this->len_ += len;
    return dropped;
  }

  // Hands every complete frame to fn as a view into this buffer, then keeps
  // only the tail that could not be parsed yet.
  template <typename F>
  void drain(F &&fn) {
    const size_t consumed = extract_frames(std::span<const uint8_t>(this->buf_.data(), this->len_), fn);
    if (consumed == 0)
      return;
    this->len_ -= consumed;
    std::copy(this->buf_.begin() + static_cast<std::ptrdiff_t>(consumed),
              this->buf_.begin() + static_cast<std::ptrdiff_t>(consumed + this->len_), this->buf_.begin());
  }

 protected:
  std::array<uint8_t, CAPACITY> buf_{};
  size_t len_{0};
};

}  // namespace esphome::xbot
