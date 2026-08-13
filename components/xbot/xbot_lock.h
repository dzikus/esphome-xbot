#pragma once

#ifdef USE_ESP32

#include "esphome/components/lock/lock.h"
#include "esphome/core/component.h"

#include "xbot.h"

namespace esphome {
namespace xbot {

class XbotRegisterLock : public lock::Lock, public Parented<XbotHub>, public Component {
 public:
  void set_dest(uint8_t d) { this->dest_ = d; }
  void set_src(uint8_t s) { this->src_ = s; }
  void set_cmd(uint8_t c) { this->cmd_ = c; }
  void set_register(uint8_t r) { this->register_ = r; }

  void setup() override {
    this->store_.init(this->parent_->persist_key(this->src_, this->register_));
    bool restored;
    if (this->store_.load(&restored)) {
      this->publish_state(restored ? lock::LOCK_STATE_LOCKED : lock::LOCK_STATE_UNLOCKED);
    }
    this->parent_->register_persist([this]() { return this->store_.flush(); });
    this->parent_->watch_register(this->src_, this->register_, [this](uint16_t raw) {
      bool on = raw != 0;
      this->store_.stage(on);
      lock::LockState want = on ? lock::LOCK_STATE_LOCKED : lock::LOCK_STATE_UNLOCKED;
      if (this->state != want) this->publish_state(want);
    });
  }

 protected:
  void control(const lock::LockCall &call) override {
    if (!call.get_state().has_value()) return;
    bool want = *call.get_state() == lock::LOCK_STATE_LOCKED;
    if (this->parent_->write_register(this->dest_, this->cmd_, this->register_, want ? 1 : 0)) {
      // Remembering a lost write here would mean the node insists the scooter
      // is unlocked when it is not.
      this->publish_state(want ? lock::LOCK_STATE_LOCKED : lock::LOCK_STATE_UNLOCKED);
    }
  }

  uint8_t dest_{0};
  uint8_t src_{0};
  uint8_t cmd_{0};
  uint8_t register_{0};
  PersistedValue<bool> store_;
};

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
