#include "xbot_protocol.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace esphome::xbot {

constexpr auto PROFILE_TABLE = std::to_array<TransportProfile>({
    {.id = Profile::NUS,
     .name = "nus",
     .service_uuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
     .write_uuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
     .notify_uuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"},
    {.id = Profile::AE00,
     .name = "ae00",
     .service_uuid = "0000ae00-0000-1000-8000-00805f9b34fb",
     .write_uuid = "0000ae01-0000-1000-8000-00805f9b34fb",
     .notify_uuid = "0000ae02-0000-1000-8000-00805f9b34fb"},
    {.id = Profile::FFE0,
     .name = "ffe0",
     .service_uuid = "0000ffe0-0000-1000-8000-00805f9b34fb",
     .write_uuid = "0000fff3-0000-1000-8000-00805f9b34fb",
     .notify_uuid = "0000fff4-0000-1000-8000-00805f9b34fb"},
    {.id = Profile::FFF0_F3F7,
     .name = "fff0-f3f7",
     .service_uuid = "0000fff0-0000-1000-8000-00805f9b34fb",
     .write_uuid = "0000fff3-0000-1000-8000-00805f9b34fb",
     .notify_uuid = "0000fff7-0000-1000-8000-00805f9b34fb"},
    {.id = Profile::FFF0_F2F1,
     .name = "fff0-f2f1",
     .service_uuid = "0000fff0-0000-1000-8000-00805f9b34fb",
     .write_uuid = "0000fff2-0000-1000-8000-00805f9b34fb",
     .notify_uuid = "0000fff1-0000-1000-8000-00805f9b34fb"},
});

std::span<const TransportProfile> profile_table() {
  return PROFILE_TABLE;
}

const TransportProfile *profile_by_id(Profile id) {
  for (const TransportProfile &p : PROFILE_TABLE) {
    if (p.id == id)
      return &p;
  }
  return nullptr;
}

static std::string lower(const std::string &s) {
  std::string out = s;
  std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

static bool has_pair(const std::vector<DiscoveredChar> &found, const std::string &service,
                     const std::string &characteristic) {
  return std::ranges::any_of(found, [&](const DiscoveredChar &d) {
    return lower(d.service) == service && lower(d.characteristic) == characteristic;
  });
}

Profile detect_profile(const std::vector<DiscoveredChar> &found) {
  for (const TransportProfile &p : PROFILE_TABLE) {
    if (has_pair(found, p.service_uuid, p.write_uuid) && has_pair(found, p.service_uuid, p.notify_uuid)) {
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
constexpr ProtocolVariant V3 = {.id = 3, .xor_key = 0x34, .read_dest = 0x20, .read_cmd = 0x61};
// Two families run the same framing in the clear.
constexpr ProtocolVariant V3_PLAIN = {.id = 3, .xor_key = 0x00, .read_dest = 0x20, .read_cmd = 0x61};

// Everything before the name-shape rule below.
constexpr auto EARLY_NAMES = std::to_array<NamePattern>({
    {.needle = "X0Robot", .result = V3_PLAIN},
    {.needle = "PRO-II", .result = V3},
    {.needle = "TK2-S", .result = V3},
    {.needle = "TK2_Y", .result = V3},
    {.needle = "M1 MAX", .result = V3},
    {.needle = "X2", .result = V3},
    {.needle = "CURTIS", .result = V3},
    {.needle = "MSEnery", .result = V3},
    {.needle = "smart", .result = V3},
    {.needle = "TK", .result = {.id = 7, .xor_key = 0x00, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "XRIDER", .result = V3},
    {.needle = "NXRIDE", .result = V3},
    {.needle = "M0Clean", .result = {.id = 6, .xor_key = 0x00, .read_dest = 0x32, .read_cmd = 0x01}},
    {.needle = "XBOT_AGV", .result = {.id = 1, .xor_key = 0x15, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "M6", .result = {.id = 0, .xor_key = 0x00, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "MiniRobot", .result = {.id = 1, .xor_key = 0x55, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "GoKart", .result = {.id = 4, .xor_key = 0x00, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "N3MTenbot", .result = {.id = 1, .xor_key = 0x00, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = " MiniPro", .result = {.id = 1, .xor_key = 0x00, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = " Ninebot", .result = {.id = 1, .xor_key = 0x00, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "Ninebot", .result = {.id = 1, .xor_key = 0x00, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "miniPLUS_", .result = {.id = 1, .xor_key = 0x00, .read_dest = 0x04, .read_cmd = 0x01}},
    {.needle = "M5Robot", .result = {.id = 1, .xor_key = 0xD8, .read_dest = 0x0A, .read_cmd = 0x01}},
});

// Everything after it.
constexpr auto LATE_NAMES = std::to_array<NamePattern>({
    {.needle = "A6Robot", .result = {.id = 2, .xor_key = 0xD8, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "M0", .result = V3},
    {.needle = "SFSO", .result = V3},
    {.needle = "MIScooter", .result = V3_PLAIN},
    {.needle = "W1", .result = {.id = 5, .xor_key = 0x37, .read_dest = 0x0A, .read_cmd = 0x01}},
    {.needle = "Plus", .result = {.id = 1, .xor_key = 0x39, .read_dest = 0x04, .read_cmd = 0x01}},
});

const NamePattern *match(std::string_view name, std::span<const NamePattern> table) {
  for (const NamePattern &p : table) {
    if (name.find(p.needle) != std::string_view::npos)
      return &p;
  }
  return nullptr;
}

}  // namespace

ProtocolVariant variant_for_name(std::string_view name) {
  const NamePattern *hit = match(name, EARLY_NAMES);
  if (hit != nullptr)
    return hit->result;

  // A name carrying M1, or an X in its first two characters, is read by shape
  // rather than by pattern: the long form is a different variant from the short
  // one, and which of the two matched picks the key.
  const bool has_m1 = name.find("M1") != std::string_view::npos;
  if (has_m1 || name.find('X') < 2) {
    if (name.size() > 6) {
      return ProtocolVariant{
          .id = 1, .xor_key = has_m1 ? uint8_t(0x7A) : uint8_t(0x18), .read_dest = 0x0A, .read_cmd = 0x01};
    }
    return V3;
  }

  hit = match(name, LATE_NAMES);
  if (hit != nullptr)
    return hit->result;

  return V3;
}

size_t build_request(std::span<uint8_t> out, uint8_t dest, uint8_t cmd, uint8_t reg, std::span<const uint8_t> payload) {
  const size_t total = payload.size() + 8;
  if (out.size() < total)
    return 0;
  out[0] = 0x55;
  out[1] = 0xAA;
  out[2] = static_cast<uint8_t>(payload.size() + 2);
  out[3] = dest;
  out[4] = cmd;
  out[5] = reg;
  std::ranges::copy(payload, out.begin() + 6);
  uint16_t sum = 0;
  for (size_t i = 2; i < total - 2; i++)
    sum = static_cast<uint16_t>(sum + out[i]);
  const uint16_t crc = static_cast<uint16_t>(~sum);
  out[total - 2] = static_cast<uint8_t>(crc & 0xFF);
  out[total - 1] = static_cast<uint8_t>(crc >> 8);
  return total;
}

size_t build_write(std::span<uint8_t> out, uint8_t dest, uint8_t cmd, uint8_t reg, int16_t value) {
  const uint16_t raw = static_cast<uint16_t>(value);
  const std::array<uint8_t, 2> payload{static_cast<uint8_t>(raw & 0xFF), static_cast<uint8_t>(raw >> 8)};
  return build_request(out, dest, cmd, reg, payload);
}

void apply_xor(std::span<uint8_t> buf, uint8_t key) {
  if (key == 0)
    return;
  for (uint8_t &b : buf)
    b = static_cast<uint8_t>(b ^ key);
}

bool checksum_ok(std::span<const uint8_t> frame) {
  if (frame.size() < 7)
    return false;
  const size_t body_end = frame.size() - 2;
  uint16_t sum = 0;
  for (size_t i = 2; i < body_end; i++)
    sum = static_cast<uint16_t>(sum + frame[i]);
  const uint16_t want = static_cast<uint16_t>(~sum);
  const uint16_t got = static_cast<uint16_t>(frame[body_end] | (frame[body_end + 1] << 8));
  return want == got;
}

Dialect identify_dialect(std::span<const uint8_t> buf) {
  if (buf.size() < 2)
    return Dialect::UNKNOWN;
  if (buf[0] == 0x55 && buf[1] == 0xAA)
    return Dialect::XIAOMI;
  if (buf[0] == 0x5A && buf[1] == 0xA5)
    return Dialect::NINEBOT;
  return Dialect::UNKNOWN;
}

uint8_t byte_diversity_pct(std::span<const uint8_t> buf) {
  if (buf.empty())
    return 0;
  std::array<bool, 256> seen{};
  size_t distinct = 0;
  for (const uint8_t b : buf) {
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
  if (data == nullptr || len == 0)
    return out;
  out.reserve(len * 3);
  for (size_t i = 0; i < len; i++) {
    if (i != 0)
      out.push_back('.');
    out.push_back(digits[data[i] >> 4]);
    out.push_back(digits[data[i] & 0x0F]);
  }
  return out;
}

}  // namespace esphome::xbot
