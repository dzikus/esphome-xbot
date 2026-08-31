#pragma once

#ifdef USE_ESP32

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "xbot.h"
#include "xbot_entity_logic.h"

namespace esphome::xbot {

// One telemetry register, optionally paired with the next one as a 32-bit
// value. divisor converts the raw count into the published unit.
class XbotRegisterSensor : public sensor::Sensor, public Parented<XbotHub>, public Component {
 public:
  void set_src(uint8_t s) { this->src_ = s; }
  void set_register(uint8_t r) { this->register_ = r; }
  void set_wide(bool b) { this->wide_ = b; }
  void set_signed(bool b) { this->signed_ = b; }
  // Separate from set_signed: a counter can be two's complement on the wire and
  // still use 0xFFFF to mean it has nothing to report.
  void set_accepts_sentinel(bool b) { this->accepts_sentinel_ = b; }
  void set_divisor(float d) { this->divisor_ = d; }
  void set_persist(bool b) { this->persist_ = b; }
  void set_range(float lo, float hi) {
    this->range_lo_ = lo;
    this->range_hi_ = hi;
    this->has_range_ = true;
  }

  void setup() override {
    if (this->persist_) {
      this->store_.init(this->parent_->persist_key(this->src_, this->register_));
      float restored;
      // Restoring skips emit_(), and values stored before the range existed
      // have never been through it.
      if (this->store_.load(&restored) &&
          (!this->has_range_ || bounded(restored, this->range_lo_, this->range_hi_).has_value())) {
        this->publish_state(restored);
      }
      this->parent_->register_persist([this]() { return this->store_.flush(); });
    }
    this->parent_->watch_register(
        this->src_, this->register_,
        [this](uint16_t raw) {
          this->low_ = raw;
          this->low_frame_ = this->parent_->frame_seq();
          if (!this->wide_)
            this->emit_(register_to_float(raw, this->signed_));
        },
        this->accepts_sentinel_);
    if (this->wide_) {
      this->parent_->watch_register(this->src_, this->register_ + 1, [this](uint16_t raw) {
        // Both halves have to come out of the same frame. One sweep block
        // carries the pair, another carries the high word alone; pairing that
        // one with a low word read seconds earlier steps the total by 65536
        // every time the counter crosses a boundary in between.
        if (this->low_frame_ == 0 || this->low_frame_ != this->parent_->frame_seq())
          return;
        this->emit_(static_cast<float>(join_words(this->low_, raw)));
      });
    }
  }

 protected:
  void emit_(float raw) {
    float value = this->divisor_ != 0.0f ? raw / this->divisor_ : raw;
    if (this->has_range_ && !bounded(value, this->range_lo_, this->range_hi_).has_value()) {
      ESP_LOGW(XBOT_TAG, "reg=0x%02X reads %.3f, outside %.3f..%.3f; dropped", this->register_, value, this->range_lo_,
               this->range_hi_);
      return;
    }
    if (this->persist_)
      this->store_.stage(value);
    if (!this->has_state() || this->state != value)
      this->publish_state(value);
  }

  uint8_t src_{0};
  uint8_t register_{0};
  uint16_t low_{0};
  uint32_t low_frame_{0};
  bool wide_{false};
  bool signed_{false};
  bool accepts_sentinel_{false};
  bool persist_{false};
  bool has_range_{false};
  float range_lo_{0.0f};
  float range_hi_{0.0f};
  float divisor_{1.0f};
  PersistedValue<float> store_;
};

// Product of two registers, for quantities the controller does not report on
// its own.
class XbotProductSensor : public sensor::Sensor, public Parented<XbotHub>, public Component {
 public:
  void set_src(uint8_t s) { this->src_ = s; }
  void set_registers(uint8_t a, uint8_t b) {
    this->reg_a_ = a;
    this->reg_b_ = b;
  }
  // Second register only. These quantities are an unsigned magnitude times a
  // signed rate, so the first half has never needed either.
  void set_b_signed(bool b) { this->b_signed_ = b; }
  void set_b_accepts_sentinel(bool b) { this->b_accepts_sentinel_ = b; }
  void set_divisor(float d) { this->divisor_ = d; }
  void set_range(float lo, float hi) {
    this->range_lo_ = lo;
    this->range_hi_ = hi;
    this->has_range_ = true;
  }

  void setup() override {
    this->parent_->register_cycle_reset([this]() {
      this->frame_a_ = 0;
      this->frame_b_ = 0;
    });
    this->parent_->watch_register(this->src_, this->reg_a_, [this](uint16_t raw) {
      this->a_ = raw;
      this->frame_a_ = this->parent_->frame_seq();
      this->emit_();
    });
    this->parent_->watch_register(
        this->src_, this->reg_b_,
        [this](uint16_t raw) {
          this->b_ = raw;
          this->frame_b_ = this->parent_->frame_seq();
          this->emit_();
        },
        this->b_accepts_sentinel_);
  }

 protected:
  void emit_() {
    // Both halves have to come out of the same frame. The lower register is
    // dispatched first, so emitting on its watcher alone would multiply this
    // sweep's value by the one the other register held a sweep ago.
    if (this->frame_a_ == 0 || this->frame_a_ != this->frame_b_)
      return;
    // Current runs negative under regeneration, so power does too.
    float product = static_cast<float>(this->a_) * register_to_float(this->b_, this->b_signed_);
    float value = this->divisor_ != 0.0f ? product / this->divisor_ : product;
    if (this->has_range_ && !bounded(value, this->range_lo_, this->range_hi_).has_value()) {
      ESP_LOGW(XBOT_TAG, "reg=0x%02X x 0x%02X reads %.3f, outside %.3f..%.3f; dropped", this->reg_a_, this->reg_b_,
               value, this->range_lo_, this->range_hi_);
      return;
    }
    if (!this->has_state() || this->state != value)
      this->publish_state(value);
  }

  uint8_t src_{0};
  uint8_t reg_a_{0};
  uint8_t reg_b_{0};
  uint16_t a_{0};
  uint16_t b_{0};
  uint32_t frame_a_{0};
  uint32_t frame_b_{0};
  bool b_signed_{false};
  bool b_accepts_sentinel_{false};
  bool has_range_{false};
  float range_lo_{0.0f};
  float range_hi_{0.0f};
  float divisor_{1.0f};
};

}  // namespace esphome::xbot

#endif  // USE_ESP32
