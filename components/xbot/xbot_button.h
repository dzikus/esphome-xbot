#pragma once

#ifdef USE_ESP32

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"
#include "xbot.h"

namespace esphome::xbot {

// The poll cycle is slow on purpose; this is the way to ask for one now, for
// instance right after walking up to the vehicle.
class XbotPollButton : public button::Button, public Parented<XbotHub> {
 protected:
  void press_action() override { this->parent_->trigger_immediate_poll(); }
};

}  // namespace esphome::xbot

#endif  // USE_ESP32
