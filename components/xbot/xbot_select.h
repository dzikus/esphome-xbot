#pragma once

#ifdef USE_ESP32

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

#include "xbot.h"

namespace esphome {
namespace xbot {

// Register value is the index into the option list.
class XbotRegisterSelect : public select::Select, public Parented<XbotHub>, public Component {
 public:
  void set_dest(uint8_t d) { this->dest_ = d; }
  void set_src(uint8_t s) { this->src_ = s; }
  void set_cmd(uint8_t c) { this->cmd_ = c; }
  void set_register(uint8_t r) { this->register_ = r; }

  void setup() override {
    this->store_.init(this->parent_->persist_key(this->src_, this->register_));
    uint8_t restored;
    if (this->store_.load(&restored)) {
      auto option = this->at(restored);
      if (option.has_value()) this->publish_state(*option);
    }
    this->parent_->register_persist([this]() { return this->store_.flush(); });
    this->parent_->watch_register(this->src_, this->register_, [this](uint16_t raw) {
      auto option = this->at(raw);
      if (!option.has_value()) return;
      this->store_.stage(static_cast<uint8_t>(raw));
      auto current = this->active_index();
      if (current.has_value() && *current == raw) return;
      this->publish_state(*option);
    });
  }

 protected:
  void control(const std::string &value) override {
    auto idx = this->index_of(value);
    if (!idx.has_value()) return;
    if (this->parent_->write_register(this->dest_, this->cmd_, this->register_,
                                      static_cast<int16_t>(*idx))) {
      this->publish_state(value);
    }
  }

  uint8_t dest_{0};
  uint8_t src_{0};
  uint8_t cmd_{0};
  uint8_t register_{0};
  PersistedValue<uint8_t> store_;
};

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
