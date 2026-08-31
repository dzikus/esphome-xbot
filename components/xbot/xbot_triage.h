#pragma once

#include <cstdint>
#include <span>

namespace esphome::xbot {

// Says what a cycle's bytes looked like when none of them assembled into a
// frame. See notifies_this_cycle_ in xbot.h for why that is worth separating.
void log_unparsed(std::span<const uint8_t> raw, uint8_t key);

}  // namespace esphome::xbot
