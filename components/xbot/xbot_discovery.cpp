#ifdef USE_ESP32

#include "xbot_discovery.h"

#include <cinttypes>
#include <cstdio>
#include <string>

#include "esphome/core/log.h"

namespace esphome {
namespace xbot {

static const char *const TAG = "xbot";

static const uint16_t CCCD_UUID16 = 0x2902;
static const uint16_t DEVICE_NAME_UUID16 = 0x2A00;

static std::string uuid_str(const esp_bt_uuid_t &u) {
  char buf[40];
  if (u.len == ESP_UUID_LEN_16) {
    snprintf(buf, sizeof(buf), "0000%04x-0000-1000-8000-00805f9b34fb", u.uuid.uuid16);
    return buf;
  }
  if (u.len == ESP_UUID_LEN_32) {
    snprintf(buf, sizeof(buf), "%08" PRIx32 "-0000-1000-8000-00805f9b34fb", u.uuid.uuid32);
    return buf;
  }
  const uint8_t *b = u.uuid.uuid128;
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[15], b[14], b[13], b[12], b[11], b[10], b[9], b[8], b[7], b[6], b[5], b[4], b[3], b[2],
           b[1], b[0]);
  return buf;
}

GattWalk walk_gatt(esp_gatt_if_t gattc_if, uint16_t conn_id) {
  GattWalk out;

  uint16_t count = 0;
  esp_gatt_status_t st = esp_ble_gattc_get_attr_count(gattc_if, conn_id, ESP_GATT_DB_ALL, 0x0001,
                                                      0xFFFF, 0, &count);
  if (st != ESP_GATT_OK) {
    ESP_LOGW(TAG, "attribute count query failed, status=%d", st);
    return out;
  }
  if (count == 0) {
    ESP_LOGW(TAG, "attribute table is empty; the GATT cache was already released");
    return out;
  }

  std::vector<esp_gattc_db_elem_t> db(count);
  st = esp_ble_gattc_get_db(gattc_if, conn_id, 0x0001, 0xFFFF, db.data(), &count);
  if (st != ESP_GATT_OK) {
    ESP_LOGW(TAG, "attribute db unavailable, status=%d", st);
    return out;
  }

  std::string current_service;
  uint16_t current_char = 0;
  for (uint16_t i = 0; i < count; i++) {
    const esp_gattc_db_elem_t &e = db[i];
    switch (e.type) {
      case ESP_GATT_DB_PRIMARY_SERVICE:
      case ESP_GATT_DB_SECONDARY_SERVICE:
        current_service = uuid_str(e.uuid);
        break;
      case ESP_GATT_DB_CHARACTERISTIC: {
        std::string cu = uuid_str(e.uuid);
        current_char = e.attribute_handle;
        if (e.uuid.len == ESP_UUID_LEN_16 && e.uuid.uuid.uuid16 == DEVICE_NAME_UUID16) {
          out.name_handle = e.attribute_handle;
        }
        if (!current_service.empty()) out.chars.push_back(DiscoveredChar{current_service, cu});
        break;
      }
      case ESP_GATT_DB_DESCRIPTOR:
        if (e.uuid.len == ESP_UUID_LEN_16 && e.uuid.uuid.uuid16 == CCCD_UUID16 &&
            current_char != 0) {
          out.cccds.push_back(CccdEntry{current_char, e.attribute_handle});
        }
        break;
      default:
        break;
    }
  }
  return out;
}

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
