#pragma once

#ifdef USE_ESP32

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include "xbot.h"
#include "xbot_entity_logic.h"

namespace esphome {
namespace xbot {

// Local only; nothing is written to the vehicle either way. The vehicle accepts
// a single connection, so reaching it from a phone means letting go here first.
class XbotBleSwitch : public switch_::Switch, public Parented<XbotHub>, public Component {
 public:
  void setup() override {
    // Shows what the hub is doing, including the changes this switch did not
    // make: the hub enables itself when no switch was configured, and a poll
    // can bring a refused vehicle back.
    this->parent_->add_ble_state_callback([this](bool s) { this->publish_state(s); });
    bool want = this->get_initial_state_with_restore_mode().value_or(true);
    this->publish_state(want);
    this->defer([this, want]() { this->parent_->set_ble_user_enabled(want); });
  }

 protected:
  void write_state(bool want) override {
    this->parent_->set_ble_user_enabled(want);
    this->publish_state(want);
  }
};

class XbotRegisterSwitch : public switch_::Switch, public Parented<XbotHub>, public Component {
 public:
  void set_dest(uint8_t d) { this->dest_ = d; }
  void set_src(uint8_t s) { this->src_ = s; }
  void set_cmd(uint8_t c) { this->cmd_ = c; }
  void set_register(uint8_t r) { this->register_ = r; }
  // Non-zero when the vehicle keeps other flags in the same register, so the
  // write has to preserve them.
  void set_bit_mask(uint16_t m) { this->bit_mask_ = m; }

  void setup() override {
    this->store_.init(this->parent_->persist_key(this->src_, this->register_));
    bool restored;
    // Only what is shown is restored. The esphome restore modes go through
    // write_state, which would push the value to the vehicle on every reboot.
    if (this->store_.load(&restored)) this->publish_state(restored);
    this->parent_->register_persist([this]() { return this->store_.flush(); });
    // The other flags in a shared register are only known from a live read. A
    // value carried over from the last connection may have been changed from
    // the vendor app since, and writing it back would undo that.
    this->parent_->register_cycle_reset([this]() { this->have_raw_ = false; });
    this->parent_->watch_register(this->src_, this->register_, [this](uint16_t raw) {
      this->raw_ = raw;
      this->have_raw_ = true;
      bool on = this->bit_mask_ != 0 ? (raw & this->bit_mask_) != 0 : raw != 0;
      this->store_.stage(on);
      if (!this->has_state() || this->state != on) this->publish_state(on);
    });
  }

 protected:
  void write_state(bool want) override {
    uint16_t value = want ? 1 : 0;
    if (this->bit_mask_ != 0) {
      if (!this->have_raw_) {
        ESP_LOGW(XBOT_TAG, "refusing write reg=0x%02X: other flags in it not read yet",
                 this->register_);
        this->status_momentary_warning("write");
        return;
      }
      value = apply_bit(this->raw_, this->bit_mask_, want);
    }
    if (this->parent_->write_register(this->dest_, this->cmd_, this->register_,
                                      static_cast<int16_t>(value))) {
      // Shown, not stored. The write goes out unacknowledged, so only a read
      // back from the vehicle is allowed to reach flash; the next sweep does
      // that.
      this->publish_state(want);
    }
  }

  uint8_t dest_{0};
  uint8_t src_{0};
  uint8_t cmd_{0};
  uint8_t register_{0};
  uint16_t bit_mask_{0};
  uint16_t raw_{0};
  bool have_raw_{false};
  PersistedValue<bool> store_;
};

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
