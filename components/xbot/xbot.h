#pragma once

#include "esphome/core/component.h"

#include <array>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <vector>

#ifdef USE_ESP32
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <esp_gattc_api.h>

#include "xbot_discovery.h"
#include "xbot_persist.h"
#include "xbot_protocol.h"
#include "xbot_receive.h"
#include "xbot_triage.h"

namespace esphome {
namespace xbot {

namespace espbt = esphome::esp32_ble_tracker;

// Shared with the entity headers, which log on their own refusal paths.
inline constexpr const char *XBOT_TAG = "xbot";

// One read request: which address, which opcode, the first register and how
// many bytes to ask for. The table is written in const.py and handed over at
// codegen, so the register numbers have one home.
struct PollEntry {
  uint8_t dest;
  uint8_t cmd;
  uint8_t reg;
  uint8_t count;
};

// Connects, subscribes, reads the register blocks, and holds the link. Settings
// are only written when an entity asks.
class XbotHub : public ble_client::BLEClientNode, public PollingComponent {
 public:
  void setup() override;
  void loop() override {}
  void update() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_profile_override(Profile p) { this->profile_override_ = p; }
  void set_probe_repeat(uint8_t n) { this->probe_repeat_ = n; }
  // Negative leaves the key to the name. Set when a vehicle's framing does not
  // match what its name implies; the triage log says which key did parse.
  void set_key_override(int16_t k) { this->key_override_ = k; }
  void add_poll_entry(uint8_t dest, uint8_t cmd, uint8_t reg, uint8_t count) {
    this->poll_table_.push_back(PollEntry{dest, cmd, reg, count});
  }
  // The vehicle takes one connection at a time.
  void set_ble_user_enabled(bool en);
  // Told at build time whether a switch will drive the flag above.
  void set_ble_switch_present() { this->ble_switch_present_ = true; }
  // The switch is not the only thing that moves the flag, so whatever displays
  // it has to hear about the changes it did not make.
  void add_ble_state_callback(std::function<void(bool)> &&cb) {
    this->ble_state_cbs_.push_back(std::move(cb));
  }

  // False when the link is down. Only ever called from an entity.
  bool write_register(uint8_t dest, uint8_t cmd, uint8_t reg, int16_t value);
  // A decoded frame proves the key as well as the link.
  bool link_ready() const {
    return this->state_ == State::LISTENING && this->write_handle_ != 0 &&
           (this->variant_known_ || this->frames_this_cycle_ > 0);
  }

  // Identifies the frame a watcher is being called from, so an entity built out
  // of two registers can tell whether both halves arrived together.
  uint32_t frame_seq() const { return this->frame_seq_; }

  // accepts_sentinel says 0xFFFF is a reading, which is what it means for a
  // register the entity reads as two's complement.
  void watch_register(uint8_t src, uint8_t reg, std::function<void(uint16_t)> &&cb,
                      bool accepts_sentinel = false) {
    this->reg_watchers_.push_back(RegWatcher{src, reg, std::move(cb), accepts_sentinel});
  }

  // Runs at the start of every connection cycle, for state that is only valid
  // while a link is up.
  void register_cycle_reset(std::function<void()> &&cb) {
    this->cycle_resets_.push_back(std::move(cb));
  }

  // The callback returns true when it staged something for flash. It runs once
  // the vehicle has been out of reach long enough for its last value to be
  // final, so a reachable vehicle costs no flash writes.
  void register_persist(std::function<bool()> &&flush) {
    this->persist_flush_.push_back(std::move(flush));
  }

  // Scoped to this vehicle, so several of them on one node keep separate state.
  uint32_t persist_key(uint8_t src, uint8_t reg);

  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }

  void trigger_immediate_poll();

 protected:
  enum class State : uint8_t {
    IDLE,
    CONNECTING,
    DISCOVERING,
    LISTENING,
  };

  void start_cycle_();
  void disconnect_();
  void finish_cycle_(const char *reason);
  void arm_watchdog_();
  void handle_search_complete_();
  void apply_name_variant_(const std::string &name);
  void continue_setup_();
  bool resolve_notify_handles_(const TransportProfile &p);
  bool register_for_notify_();
  bool resolve_write_handle_(const TransportProfile &p);
  void write_cccd_();
  void reset_cycle_state_();
  void start_probes_();
  void send_probe_(size_t index, uint8_t repeat);
  void mark_for_verify_(uint8_t dest, uint8_t reg);
  void run_verify_();
  void handle_notify_(const esp_ble_gattc_cb_param_t::gattc_notify_evt_param &n);
  void report_frames_();
  void dispatch_registers_(std::span<const uint8_t> frame);
  uint32_t flush_persist_();
  void check_persist_flush_();

  std::vector<PollEntry> poll_table_;
  uint8_t probe_repeat_{2};
  State state_{State::IDLE};
  Profile profile_{Profile::UNKNOWN};
  Profile profile_override_{Profile::UNKNOWN};
  uint16_t name_handle_{0};
  std::vector<CccdEntry> cccd_map_;
  struct RegWatcher {
    uint8_t src;
    uint8_t reg;
    std::function<void(uint16_t)> cb;
    bool accepts_sentinel;
  };
  std::vector<RegWatcher> reg_watchers_;
  std::vector<std::function<bool()>> persist_flush_;
  uint32_t last_data_ms_{0};
  bool persist_pending_{false};
  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};
  uint16_t cccd_handle_{0};
  bool probes_started_{false};
  // False so a restored "on" still runs the full enable path. Starting true
  // makes that restore a no-op and the first connection waits a whole poll.
  // setup() flips it when no switch was configured to do the flipping.
  bool ble_user_enabled_{false};
  bool ble_switch_present_{false};
  std::vector<std::function<void(bool)>> ble_state_cbs_;
  // A write into a full l2cap buffer is dropped without an error.
  bool link_congested_{false};
  size_t pending_probe_index_{0};
  uint8_t pending_probe_repeat_{0};
  bool probe_deferred_{false};
  uint8_t congestion_waits_{0};
  // Replaced once the device name is read.
  uint8_t xor_key_{XOR_KEY_VARIANT3};
  int16_t key_override_{-1};
  // The name is a property of the vehicle, not of the connection. Holding the
  // resolved key means one cycle whose name read fails cannot quietly go back
  // to obfuscating frames for a vehicle that wants them plain.
  bool variant_known_{false};
  // True while a sweep is walking the table. See trigger_immediate_poll.
  bool sweep_active_{false};
  // Blocks a write left unconfirmed, one bit per table entry. Writes inside the
  // same verify window share a timer.
  uint32_t verify_mask_{0};
  static constexpr size_t RX_CAPACITY = 512;
  RxAccumulator<RX_CAPACITY> rx_;
  uint32_t frames_this_cycle_{0};
  // Counted apart from frames: a vehicle that says nothing and a vehicle whose
  // bytes will not parse are different faults with the same symptom.
  uint32_t notifies_this_cycle_{0};
  uint32_t frame_seq_{0};
  std::vector<std::function<void()>> cycle_resets_;
  // The first payload of a cycle, kept raw for the end-of-cycle triage. Sized
  // to the largest payload an ATT notification can carry, so it never grows.
  std::array<uint8_t, 244> first_chunk_{};
  size_t first_chunk_len_{0};
  // Set when the name resolved to a variant this build cannot decode.
  bool variant_refused_{false};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
};

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
