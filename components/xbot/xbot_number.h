#pragma once

#ifdef USE_ESP32

#include <cinttypes>
#include <cmath>
#include <cstdint>

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "xbot.h"
#include "xbot_entity_logic.h"

namespace esphome::xbot {

class XbotRegisterNumber : public number::Number, public Parented<XbotHub>, public Component {
 public:
  void set_dest(uint8_t d) { this->dest_ = d; }
  void set_src(uint8_t s) { this->src_ = s; }
  void set_cmd(uint8_t c) { this->cmd_ = c; }
  void set_register(uint8_t r) { this->register_ = r; }
  void set_scale(float s) { this->scale_ = s; }
  // Scale factor comes from the vehicle, read every connection.
  void set_scale_from(uint8_t reg, float divisor) {
    this->scale_reg_ = reg;
    this->scale_divisor_ = divisor;
  }

  void setup() override {
    this->store_.init(this->parent_->persist_key(this->src_, this->register_));
    float restored;
    // Only what is shown is restored. Going through control() would push the
    // value back to the vehicle on every reboot of the node.
    if (this->store_.load(&restored) && restored >= this->traits.get_min_value() &&
        restored <= this->traits.get_max_value()) {
      this->publish_state(restored);
    }
    this->parent_->register_persist([this]() { return this->store_.flush(); });
    if (this->scale_reg_ != 0) {
      // A factor carried over from the last connection may not be this
      // vehicle's any more, and the same count then means a different speed.
      this->parent_->register_cycle_reset([this]() {
        this->scale_known_ = false;
        this->warned_no_scale_ = false;
      });
      this->parent_->watch_register(this->src_, this->scale_reg_, [this](uint16_t raw) {
        auto scale = scale_from_factor(raw, this->scale_divisor_);
        if (!scale.has_value())
          return;
        this->scale_ = *scale;
        this->scale_known_ = true;
      });
    }
    this->parent_->watch_register(this->src_, this->register_, [this](uint16_t raw) {
      // Same gate as the write path. Without the vehicle's own scale the count
      // would divide by 1 and show a limit of several hundred km/h, and that
      // reading is what reaches flash.
      if (this->scale_reg_ != 0 && !this->scale_known_) {
        // Once a cycle: a vehicle that never answers the scale register would
        // otherwise leave this entity unknown and silent.
        if (!this->warned_no_scale_) {
          this->warned_no_scale_ = true;
          ESP_LOGW(XBOT_TAG,
                   "reg=0x%02X ignored: reg=0x%02X has not been reported, so counts "
                   "cannot be turned into speed",
                   this->register_, this->scale_reg_);
        }
        return;
      }
      float step = this->traits.get_step();
      auto value = readback_value(raw, this->scale_, step, this->traits.get_min_value(), this->traits.get_max_value());
      if (!value.has_value()) {
        ESP_LOGW(XBOT_TAG, "reg=0x%02X reads %u counts, outside min_value..max_value; widen them in the yaml",
                 this->register_, raw);
        return;
      }
      this->store_.stage(*value);
      if (!this->has_state() || std::fabs(this->state - *value) > step / 4.0f)
        this->publish_state(*value);
    });
  }

 protected:
  void control(float value) override {
    // The vehicle reports the scale, and it does not arrive with the first
    // frame. Writing before it lands sends the value unscaled: 35 counts is a
    // 1.5 km/h limit, not 35 km/h.
    if (this->scale_reg_ != 0 && !this->scale_known_) {
      ESP_LOGW(XBOT_TAG, "refusing write reg=0x%02X: the vehicle has not reported the scale yet", this->register_);
      this->status_momentary_warning("write");
      return;
    }
    auto raw = write_count(value, this->scale_);
    if (!raw.has_value()) {
      ESP_LOGW(XBOT_TAG, "refusing write reg=0x%02X: %.2f does not fit the register", this->register_, value);
      this->status_momentary_warning("write");
      return;
    }
    if (this->parent_->write_register(this->dest_, this->cmd_, this->register_, *raw)) {
      this->publish_state(value);
    }
  }

  uint8_t dest_{0};
  uint8_t src_{0};
  uint8_t cmd_{0};
  uint8_t register_{0};
  uint8_t scale_reg_{0};
  float scale_divisor_{0.0f};
  float scale_{1.0f};
  bool scale_known_{false};
  bool warned_no_scale_{false};
  PersistedValue<float> store_;
};

}  // namespace esphome::xbot

#endif  // USE_ESP32
