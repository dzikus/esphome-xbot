#pragma once

#ifdef USE_ESP32

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <cstdio>
#include <string>
#include <vector>

#include "xbot.h"

namespace esphome {
namespace xbot {

// Several registers rendered as one hex string, in the order they are added.
class XbotRegisterTextSensor : public text_sensor::TextSensor,
                               public Parented<XbotHub>,
                               public Component {
 public:
  void set_src(uint8_t s) { this->src_ = s; }
  void add_register(uint8_t r) {
    this->regs_.push_back(r);
    this->values_.push_back(0);
    this->seen_.push_back(false);
  }

  void setup() override {
    for (size_t i = 0; i < this->regs_.size(); i++) {
      this->parent_->watch_register(this->src_, this->regs_[i], [this, i](uint16_t raw) {
        this->values_[i] = raw;
        this->seen_[i] = true;
        this->emit_();
      });
    }
  }

 protected:
  void emit_() {
    for (size_t i = 0; i < this->seen_.size(); i++) {
      if (!this->seen_[i]) return;
    }
    std::string out;
    char buf[8];
    for (uint16_t v : this->values_) {
      snprintf(buf, sizeof(buf), "%04X", v);
      out += buf;
    }
    if (!this->has_state() || this->state != out) this->publish_state(out);
  }

  uint8_t src_{0};
  std::vector<uint8_t> regs_;
  std::vector<uint16_t> values_;
  std::vector<bool> seen_;
};

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
