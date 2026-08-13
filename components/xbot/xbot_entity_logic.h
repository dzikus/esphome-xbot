#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

// The decisions an entity makes about a register value, kept free of esphome so
// the refusal paths can be tested. An empty optional always means refused, and
// the caller publishes nothing and writes nothing.

namespace esphome {
namespace xbot {

// Every speed limit is a count scaled by the wheel factor the vehicle reports.
// Zero means it has not reported one, and nothing may be scaled yet.
inline std::optional<float> scale_from_factor(uint16_t raw, float divisor) {
  if (raw == 0) return std::nullopt;
  return divisor / static_cast<float>(raw);
}

// A count read back from the vehicle, in the entity's unit. Refused outside the
// declared bounds: that is a wrong scale or a wrong register, not a setting
// this slider could have produced.
inline std::optional<float> readback_value(uint16_t raw, float scale, float step, float min_value,
                                           float max_value) {
  float value = scale != 0.0f ? static_cast<float>(raw) / scale : static_cast<float>(raw);
  // The write truncates, so the round trip lands just under a whole step.
  if (step > 0.0f) value = roundf(value / step) * step;
  if (value < min_value || value > max_value) return std::nullopt;
  return value;
}

// The count to send for a value the user asked for. Truncated, not rounded:
// rounding lands a count above what the vehicle reads back.
inline std::optional<int16_t> write_count(float value, float scale) {
  int32_t raw = static_cast<int32_t>(value * scale);
  if (raw < INT16_MIN || raw > INT16_MAX) return std::nullopt;
  return static_cast<int16_t>(raw);
}

// A flag sharing its register with others: the write carries the bits it is not
// changing, so it cannot clear a setting made from the vendor app.
inline uint16_t apply_bit(uint16_t raw, uint16_t mask, bool on) {
  return on ? static_cast<uint16_t>(raw | mask) : static_cast<uint16_t>(raw & ~mask);
}

// A reading is refused when it is not something the quantity can be.
inline std::optional<float> bounded(float value, float lo, float hi) {
  if (value < lo || value > hi) return std::nullopt;
  return value;
}

// Some registers are two's complement on the wire.
inline float register_to_float(uint16_t raw, bool is_signed) {
  return is_signed ? static_cast<float>(static_cast<int16_t>(raw)) : static_cast<float>(raw);
}

// Two registers as one count, low word first.
inline uint32_t join_words(uint16_t low, uint16_t high) {
  return (static_cast<uint32_t>(high) << 16) | static_cast<uint32_t>(low);
}

}  // namespace xbot
}  // namespace esphome
