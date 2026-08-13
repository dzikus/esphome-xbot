#pragma once

#ifdef USE_ESP32

#include <cstdint>
#include <vector>

#include <esp_gattc_api.h>

#include "xbot_protocol.h"

namespace esphome {
namespace xbot {

// Handles are collected during the walk because esphome frees the discovered
// database as soon as every client is established, and a later lookup finds an
// empty table.
struct CccdEntry {
  uint16_t char_handle;
  uint16_t cccd_handle;
};

struct GattWalk {
  std::vector<DiscoveredChar> chars;
  std::vector<CccdEntry> cccds;
  uint16_t name_handle{0};
};

// Reads the whole attribute table in one pass. Only yields anything while the
// database is still alive, which means inside ESP_GATTC_SEARCH_CMPL_EVT and not
// from a timer armed there. An empty result is normal.
GattWalk walk_gatt(esp_gatt_if_t gattc_if, uint16_t conn_id);

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
