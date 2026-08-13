#include "xbot_protocol.h"

#include <algorithm>
#include <cctype>

namespace esphome {
namespace xbot {

const TransportProfile PROFILE_TABLE[] = {
    {Profile::NUS, "nus", "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
     "6e400002-b5a3-f393-e0a9-e50e24dcca9e", "6e400003-b5a3-f393-e0a9-e50e24dcca9e"},
    {Profile::AE00, "ae00", "0000ae00-0000-1000-8000-00805f9b34fb",
     "0000ae01-0000-1000-8000-00805f9b34fb", "0000ae02-0000-1000-8000-00805f9b34fb"},
    {Profile::FFE0, "ffe0", "0000ffe0-0000-1000-8000-00805f9b34fb",
     "0000fff3-0000-1000-8000-00805f9b34fb", "0000fff4-0000-1000-8000-00805f9b34fb"},
    {Profile::FFF0_F3F7, "fff0-f3f7", "0000fff0-0000-1000-8000-00805f9b34fb",
     "0000fff3-0000-1000-8000-00805f9b34fb", "0000fff7-0000-1000-8000-00805f9b34fb"},
    {Profile::FFF0_F2F1, "fff0-f2f1", "0000fff0-0000-1000-8000-00805f9b34fb",
     "0000fff2-0000-1000-8000-00805f9b34fb", "0000fff1-0000-1000-8000-00805f9b34fb"},
};

const size_t PROFILE_TABLE_LEN = sizeof(PROFILE_TABLE) / sizeof(PROFILE_TABLE[0]);

const TransportProfile *profile_by_id(Profile id) {
  for (size_t i = 0; i < PROFILE_TABLE_LEN; i++) {
    if (PROFILE_TABLE[i].id == id) return &PROFILE_TABLE[i];
  }
  return nullptr;
}

static std::string lower(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

static bool has_pair(const std::vector<DiscoveredChar> &found, const std::string &service,
                     const std::string &characteristic) {
  for (const auto &d : found) {
    if (lower(d.service) == service && lower(d.characteristic) == characteristic) return true;
  }
  return false;
}

Profile detect_profile(const std::vector<DiscoveredChar> &found) {
  for (size_t i = 0; i < PROFILE_TABLE_LEN; i++) {
    const TransportProfile &p = PROFILE_TABLE[i];
    if (has_pair(found, p.service_uuid, p.write_uuid) &&
        has_pair(found, p.service_uuid, p.notify_uuid)) {
      return p.id;
    }
  }
  return Profile::UNKNOWN;
}

namespace {

struct NamePattern {
  const char *needle;
  ProtocolVariant result;
};

// Variant 3 speaks 55 AA framing at address 0x20 with read opcode 0x61.
constexpr ProtocolVariant V3 = {3, 0x34, 0x20, 0x61};
// Two families run the same framing in the clear.
constexpr ProtocolVariant V3_PLAIN = {3, 0x00, 0x20, 0x61};

// Everything before the name-shape rule below.
const NamePattern EARLY_NAMES[] = {
    {"X0Robot", V3_PLAIN},      {"PRO-II", V3},
    {"TK2-S", V3},              {"TK2_Y", V3},
    {"M1 MAX", V3},             {"X2", V3},
    {"CURTIS", V3},             {"MSEnery", V3},
    {"smart", V3},              {"TK", {7, 0x00, 0x0A, 0x01}},
    {"XRIDER", V3},             {"NXRIDE", V3},
    {"M0Clean", {6, 0x00, 0x32, 0x01}},
    {"XBOT_AGV", {1, 0x15, 0x0A, 0x01}},
    {"M6", {0, 0x00, 0x0A, 0x01}},
    {"MiniRobot", {1, 0x55, 0x0A, 0x01}},
    {"GoKart", {4, 0x00, 0x0A, 0x01}},
    {"N3MTenbot", {1, 0x00, 0x0A, 0x01}},
    {" MiniPro", {1, 0x00, 0x0A, 0x01}},
    {" Ninebot", {1, 0x00, 0x0A, 0x01}},
    {"Ninebot", {1, 0x00, 0x0A, 0x01}},
    {"miniPLUS_", {1, 0x00, 0x04, 0x01}},
    {"M5Robot", {1, 0xD8, 0x0A, 0x01}},
};

// Everything after it.
const NamePattern LATE_NAMES[] = {
    {"A6Robot", {2, 0xD8, 0x0A, 0x01}},
    {"M0", V3},
    {"SFSO", V3},
    {"MIScooter", V3_PLAIN},
    {"W1", {5, 0x37, 0x0A, 0x01}},
    {"Plus", {1, 0x39, 0x04, 0x01}},
};

const NamePattern *match(std::string_view name, const NamePattern *table, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (name.find(table[i].needle) != std::string_view::npos) return &table[i];
  }
  return nullptr;
}

}  // namespace

ProtocolVariant variant_for_name(std::string_view name) {
  const NamePattern *hit =
      match(name, EARLY_NAMES, sizeof(EARLY_NAMES) / sizeof(EARLY_NAMES[0]));
  if (hit != nullptr) return hit->result;

  // A name carrying M1, or an X in its first two characters, is read by shape
  // rather than by pattern: the long form is a different variant from the short
  // one, and which of the two matched picks the key.
  bool has_m1 = name.find("M1") != std::string_view::npos;
  if (has_m1 || name.find('X') < 2) {
    if (name.size() > 6)
      return ProtocolVariant{1, has_m1 ? uint8_t(0x7A) : uint8_t(0x18), 0x0A, 0x01};
    return V3;
  }

  hit = match(name, LATE_NAMES, sizeof(LATE_NAMES) / sizeof(LATE_NAMES[0]));
  if (hit != nullptr) return hit->result;

  return V3;
}

size_t build_request(std::span<uint8_t> out, uint8_t dest, uint8_t cmd, uint8_t reg,
                     std::span<const uint8_t> payload) {
  size_t total = payload.size() + 8;
  if (out.size() < total) return 0;
  out[0] = 0x55;
  out[1] = 0xAA;
  out[2] = static_cast<uint8_t>(payload.size() + 2);
  out[3] = dest;
  out[4] = cmd;
  out[5] = reg;
  std::copy(payload.begin(), payload.end(), out.begin() + 6);
  uint16_t sum = 0;
  for (size_t i = 2; i < total - 2; i++) sum = static_cast<uint16_t>(sum + out[i]);
  uint16_t crc = static_cast<uint16_t>(~sum);
  out[total - 2] = static_cast<uint8_t>(crc & 0xFF);
  out[total - 1] = static_cast<uint8_t>(crc >> 8);
  return total;
}

size_t build_write(std::span<uint8_t> out, uint8_t dest, uint8_t cmd, uint8_t reg,
                   int16_t value) {
  uint16_t raw = static_cast<uint16_t>(value);
  const uint8_t payload[] = {static_cast<uint8_t>(raw & 0xFF), static_cast<uint8_t>(raw >> 8)};
  return build_request(out, dest, cmd, reg, payload);
}

void apply_xor(std::span<uint8_t> buf, uint8_t key) {
  if (key == 0) return;
  for (uint8_t &b : buf) b = static_cast<uint8_t>(b ^ key);
}

bool checksum_ok(std::span<const uint8_t> frame) {
  if (frame.size() < 7) return false;
  size_t body_end = frame.size() - 2;
  uint16_t sum = 0;
  for (size_t i = 2; i < body_end; i++) sum = static_cast<uint16_t>(sum + frame[i]);
  uint16_t want = static_cast<uint16_t>(~sum);
  uint16_t got = static_cast<uint16_t>(frame[body_end] | (frame[body_end + 1] << 8));
  return want == got;
}

Dialect identify_dialect(std::span<const uint8_t> buf) {
  if (buf.size() < 2) return Dialect::UNKNOWN;
  if (buf[0] == 0x55 && buf[1] == 0xAA) return Dialect::XIAOMI;
  if (buf[0] == 0x5A && buf[1] == 0xA5) return Dialect::NINEBOT;
  return Dialect::UNKNOWN;
}

uint8_t byte_diversity_pct(std::span<const uint8_t> buf) {
  if (buf.empty()) return 0;
  bool seen[256] = {false};
  size_t distinct = 0;
  for (uint8_t b : buf) {
    if (!seen[b]) {
      seen[b] = true;
      distinct++;
    }
  }
  return static_cast<uint8_t>((distinct * 100) / buf.size());
}

std::string hex_dump(const uint8_t *data, size_t len) {
  static const char *digits = "0123456789ABCDEF";
  std::string out;
  if (data == nullptr || len == 0) return out;
  out.reserve(len * 3);
  for (size_t i = 0; i < len; i++) {
    if (i != 0) out.push_back('.');
    out.push_back(digits[data[i] >> 4]);
    out.push_back(digits[data[i] & 0x0F]);
  }
  return out;
}

}  // namespace xbot
}  // namespace esphome
