#include "xbot.h"

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

namespace esphome {
namespace xbot {

static const char *const TAG = XBOT_TAG;

static const uint32_t WHOLE_CYCLE_TIMEOUT_MS = 60000;
static const uint32_t POST_CONNECT_SETTLE_MS = 800;
// Fallback in case the descriptor write never reports back. The vehicle only
// stays awake for about five minutes, so a stuck subscription must not cost the
// whole window.
static const uint32_t CCCD_WAIT_MS = 2500;
static const uint32_t NAME_READ_TIMEOUT_MS = 2000;
// Only for the no-profile-matched case; a matched link is held open, which also
// keeps the vehicle from powering itself off.
static const uint32_t LISTEN_HOLD_MS = 30000;
static const uint32_t PROBE_INTERVAL_MS = 300;
static const uint8_t MAX_CONGESTION_WAITS = 10;
// How long after a write to read the register back. Nothing acknowledges a
// write, so the reply to this read is the only thing that says the vehicle took
// it. Reading sooner than the vehicle applies reports the old value as the new
// state.
static const uint32_t WRITE_VERIFY_MS = 2000;
// 0x52 is the write type this vehicle answers on.
static const bool WRITE_WITH_RESPONSE = false;
// A gap this long means the ride is over and the last reading will not be
// improved on. Waiting keeps it to one flash write per usage cycle.
static const uint32_t PERSIST_IDLE_MS = 600000;

void XbotHub::setup() {
  // BLEClient sets itself up after this component and would otherwise auto
  // connect outside a cycle, so the disable has to land after that.
  this->defer([this]() { this->parent()->set_enabled(false); });
  // The switch platform owns this flag when it is configured. Without it
  // nothing would ever turn the radio on and the hub would sit idle for good,
  // so a config that leaves the switch out runs enabled.
  if (!this->ble_switch_present_) {
    this->ble_user_enabled_ = true;
    ESP_LOGD(TAG, "no bluetooth switch configured, staying enabled");
    for (auto &cb : this->ble_state_cbs_) cb(true);
    // The switch starts the first cycle when it restores; with none, start here.
    this->defer([this]() { this->start_cycle_(); });
  }
}

void XbotHub::dump_config() {
  ESP_LOGCONFIG(TAG, "XBOT hub:");
  ESP_LOGCONFIG(TAG, "  MAC: %s", this->parent()->address_str());
  ESP_LOGCONFIG(TAG, "  BLE enabled: %s", YESNO(this->ble_user_enabled_));
  ESP_LOGCONFIG(TAG, "  Obfuscation key: 0x%02X", this->xor_key_);
  ESP_LOGCONFIG(TAG, "  Register blocks: %u, asked for %ux each",
                static_cast<unsigned>(this->poll_table_.size()), this->probe_repeat_);
  if (this->profile_override_ != Profile::UNKNOWN) {
    const TransportProfile *p = profile_by_id(this->profile_override_);
    ESP_LOGCONFIG(TAG, "  Profile: forced to %s", p != nullptr ? p->name : "?");
  } else {
    ESP_LOGCONFIG(TAG, "  Profile: auto-detect");
  }
}

void XbotHub::update() {
  this->check_persist_flush_();
  if (!this->ble_user_enabled_) return;
  if (this->variant_refused_) return;
  if (this->state_ == State::LISTENING) {
    // The link is held on purpose, so re-read over it. Without this the values
    // would freeze at the first sweep until the vehicle drops the connection.
    if (this->sweep_active_) {
      ESP_LOGD(TAG, "sweep still running, letting it finish");
      return;
    }
    this->probes_started_ = false;
    this->start_probes_();
    return;
  }
  if (this->state_ != State::IDLE) {
    ESP_LOGD(TAG, "cycle still active (state=%d), skipping tick", static_cast<int>(this->state_));
    return;
  }
  this->start_cycle_();
}

void XbotHub::trigger_immediate_poll() {
  if (!this->ble_user_enabled_) {
    ESP_LOGW(TAG, "poll requested while BLE is switched off, ignoring");
    return;
  }
  if (this->variant_refused_) {
    // The button is the only way back when no bluetooth switch was configured,
    // since that switch is what normally clears this.
    ESP_LOGW(TAG, "retrying a vehicle this build refused; switch it off to stop");
    this->variant_refused_ = false;
    this->status_clear_error();
  }
  // A held link sits in LISTENING as its resting state, so refusing there would
  // mean the button does nothing in the configuration it is most wanted in.
  if (this->state_ == State::LISTENING) {
    // Restarting a sweep that is already walking the table would reset it to the
    // first entry every time, so a button pressed faster than a sweep completes
    // would mean the last entries are never read at all.
    if (this->sweep_active_) {
      ESP_LOGD(TAG, "poll requested while a sweep is running, letting it finish");
      return;
    }
    this->probes_started_ = false;
    this->start_probes_();
    return;
  }
  if (this->state_ != State::IDLE) {
    ESP_LOGW(TAG, "poll requested while a cycle is open, ignoring");
    return;
  }
  this->start_cycle_();
}

void XbotHub::set_ble_user_enabled(bool en) {
  if (en == this->ble_user_enabled_) return;
  this->ble_user_enabled_ = en;
  for (auto &cb : this->ble_state_cbs_) cb(en);
  // Switching back on is the way to ask for another look, after a firmware
  // update renamed the vehicle or a different one took over this ble_client.
  if (en) {
    this->variant_refused_ = false;
    this->status_clear_error();
  }
  if (!en) {
    if (this->state_ != State::IDLE) {
      // Tear the link down now: the point of switching this off is that
      // something else wants the vehicle.
      this->disconnect_();
    } else {
      // Covers the boot race where BLEClient leaves itself enabled.
      this->parent()->set_enabled(false);
    }
    ESP_LOGI(TAG, "BLE switched off, link released");
    return;
  }
  ESP_LOGI(TAG, "BLE switched on");
  this->start_cycle_();
}

void XbotHub::reset_cycle_state_() {
  this->frames_this_cycle_ = 0;
  this->notifies_this_cycle_ = 0;
  this->first_chunk_len_ = 0;
  this->notify_handle_ = 0;
  this->cccd_handle_ = 0;
  this->write_handle_ = 0;
  this->name_handle_ = 0;
  this->probes_started_ = false;
  this->sweep_active_ = false;
  this->verify_mask_ = 0;
  this->link_congested_ = false;
  this->probe_deferred_ = false;
  this->congestion_waits_ = 0;
  // Kept once resolved; see variant_known_ in xbot.h.
  if (this->key_override_ >= 0) {
    this->xor_key_ = static_cast<uint8_t>(this->key_override_);
  } else if (!this->variant_known_) {
    this->xor_key_ = XOR_KEY_VARIANT3;
  }
  this->cccd_map_.clear();
  this->rx_.clear();
  for (auto &cb : this->cycle_resets_) cb();
}

void XbotHub::start_cycle_() {
  ESP_LOGD(TAG, "starting cycle");
  this->state_ = State::CONNECTING;
  this->reset_cycle_state_();
  this->arm_watchdog_();
  this->parent()->set_enabled(true);
}

void XbotHub::arm_watchdog_() {
  this->set_timeout("cycle_watchdog", WHOLE_CYCLE_TIMEOUT_MS, [this]() {
    if (this->state_ == State::LISTENING) {
      ESP_LOGD(TAG, "watchdog ignored, link is held on purpose");
      return;
    }
    if (this->state_ == State::CONNECTING) {
      // Not a fault: the vehicle is usually away.
      ESP_LOGD(TAG, "no answer, the vehicle is out of range or asleep");
    } else {
      ESP_LOGW(TAG, "cycle watchdog fired in state=%d", static_cast<int>(this->state_));
    }
    this->disconnect_();
  });
}

void XbotHub::disconnect_() {
  this->cancel_timeout("cycle_watchdog");
  this->cancel_timeout("listen_hold");
  this->cancel_timeout("cccd_wait");
  this->cancel_timeout("name_read");
  this->cancel_timeout("probe");
  this->cancel_timeout("verify");
  this->parent()->set_enabled(false);
  this->finish_cycle_("disconnect requested");
}

void XbotHub::finish_cycle_(const char *reason) {
  if (this->state_ == State::IDLE) return;
  ESP_LOGD(TAG, "cycle finished: %s (notifies=%" PRIu32 ", frames=%" PRIu32 ")", reason,
           this->notifies_this_cycle_, this->frames_this_cycle_);
  // Bytes arrived and not one of them assembled. Only worth saying at the end
  // of a cycle, because a reply split across two notifications is ordinary.
  if (this->notifies_this_cycle_ > 0 && this->frames_this_cycle_ == 0 &&
      this->first_chunk_len_ != 0) {
    log_unparsed(std::span<const uint8_t>(this->first_chunk_.data(), this->first_chunk_len_),
                 this->xor_key_);
  }
  this->state_ = State::IDLE;
  if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(false);
}

void XbotHub::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                  esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "open failed, status=%d", param->open.status);
        this->disconnect_();
        break;
      }
      if (this->state_ == State::IDLE) {
        // Unsolicited connect; the stack can reconnect on its own. Adopt it so
        // the link has an owner.
        ESP_LOGD(TAG, "adopting unexpected connection as a cycle");
        this->state_ = State::CONNECTING;
        // Same slate as a cycle we started ourselves, or probes_started_ from
        // the last one silences this one entirely.
        this->reset_cycle_state_();
        this->arm_watchdog_();
      }
      this->state_ = State::DISCOVERING;
      if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(true);
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      // Characteristic lookups only resolve synchronously here, and nothing
      // else marks the node established.
      this->node_state = espbt::ClientState::ESTABLISHED;
      // Must run here and not from a timer: esphome frees the discovered GATT
      // database as soon as every client is established ("services released"),
      // and a deferred lookup then finds an empty attribute table.
      this->handle_search_complete_();
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      this->handle_notify_(param->notify);
      break;
    }

    case ESP_GATTC_READ_CHAR_EVT: {
      if (param->read.handle != this->name_handle_ || this->name_handle_ == 0) break;
      // The read can land after the link is gone; conn_id is not cleared until
      // the close event, so the base class filter lets it through.
      if (this->state_ != State::DISCOVERING) break;
      this->cancel_timeout("name_read");
      if (param->read.status != ESP_GATT_OK || param->read.value_len == 0) {
        ESP_LOGW(TAG, "device name unreadable, status=%d", param->read.status);
        this->continue_setup_();
        break;
      }
      this->apply_name_variant_(std::string(reinterpret_cast<const char *>(param->read.value),
                                            param->read.value_len));
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "register for notify failed, status=%d", param->reg_for_notify.status);
        break;
      }
      // Local registration only routes what arrives; the device stays silent
      // until its CCCD says otherwise.
      this->write_cccd_();
      break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT: {
      if (param->write.handle != this->cccd_handle_) break;
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "CCCD write rejected, status=%d", param->write.status);
      } else {
        ESP_LOGI(TAG, "CCCD 0x%04X written, notifications enabled", this->cccd_handle_);
      }
      this->start_probes_();
      break;
    }

    case ESP_GATTC_CONGEST_EVT: {
      if (param->congest.conn_id != this->parent()->get_conn_id()) break;
      this->link_congested_ = param->congest.congested;
      ESP_LOGD(TAG, "l2cap %scongested", this->link_congested_ ? "" : "un");
      if (!this->link_congested_ && this->probe_deferred_) {
        this->probe_deferred_ = false;
        this->cancel_timeout("probe");
        this->send_probe_(this->pending_probe_index_, this->pending_probe_repeat_);
      }
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      this->link_congested_ = false;
      this->probe_deferred_ = false;
      this->cancel_timeout("cycle_watchdog");
      this->cancel_timeout("listen_hold");
      this->cancel_timeout("cccd_wait");
      // Leaving this armed lets setup finish on a link that is already gone,
      // which lands the hub in LISTENING with a stale write handle.
      this->cancel_timeout("name_read");
      this->cancel_timeout("probe");
      // A verify armed just before the drop would otherwise fire into the next
      // connection and read a block nobody asked about.
      this->cancel_timeout("verify");
      this->finish_cycle_("link dropped");
      break;
    }

    default:
      break;
  }
}

void XbotHub::handle_search_complete_() {
  GattWalk walk = walk_gatt(static_cast<esp_gatt_if_t>(this->parent()->get_gattc_if()),
                            this->parent()->get_conn_id());
  this->name_handle_ = walk.name_handle;
  this->cccd_map_ = std::move(walk.cccds);
  std::vector<DiscoveredChar> found = std::move(walk.chars);

  if (found.empty()) {
    // No attribute table to walk, so ask for each profile's pair by UUID; the
    // per-characteristic lookup still works even when the dump does not.
    for (size_t i = 0; i < PROFILE_TABLE_LEN; i++) {
      const TransportProfile &cand = PROFILE_TABLE[i];
      auto svc = espbt::ESPBTUUID::from_raw(cand.service_uuid);
      if (this->parent()->get_characteristic(svc, espbt::ESPBTUUID::from_raw(cand.write_uuid)) &&
          this->parent()->get_characteristic(svc, espbt::ESPBTUUID::from_raw(cand.notify_uuid))) {
        found.push_back(DiscoveredChar{cand.service_uuid, cand.write_uuid});
        found.push_back(DiscoveredChar{cand.service_uuid, cand.notify_uuid});
        ESP_LOGI(TAG, "profile matched by direct lookup: %s", cand.name);
        break;
      }
    }
  }

  Profile detected = this->profile_override_ != Profile::UNKNOWN ? this->profile_override_
                                                                 : detect_profile(found);
  this->profile_ = detected;

  const TransportProfile *p = profile_by_id(detected);
  if (p == nullptr) {
    ESP_LOGW(TAG, "no known transport profile on this device");
    ESP_LOGW(TAG, "set profile: in the yaml to force one");
    this->set_timeout("listen_hold", LISTEN_HOLD_MS, [this]() { this->disconnect_(); });
    return;
  }

  ESP_LOGI(TAG, "transport profile: %s", p->name);

  // Every lookup that reads the attribute table has to finish before this
  // function returns. Esphome frees the discovered database the moment the last
  // client is established, and the name read below completes after that point,
  // so anything resolved from its callback finds nothing.
  if (!this->resolve_notify_handles_(*p)) {
    this->disconnect_();
    return;
  }
  if (!this->resolve_write_handle_(*p)) {
    this->disconnect_();
    return;
  }

  // The protocol variant follows from the advertised name, which the GATT
  // device-name characteristic repeats. Settle it before anything goes out,
  // because the wrong key turns every frame into noise the vehicle discards.
  if (this->name_handle_ != 0 &&
      esp_ble_gattc_read_char(static_cast<esp_gatt_if_t>(this->parent()->get_gattc_if()), this->parent()->get_conn_id(),
                              this->name_handle_, ESP_GATT_AUTH_REQ_NONE) == ESP_OK) {
    this->set_timeout("name_read", NAME_READ_TIMEOUT_MS, [this]() {
      ESP_LOGW(TAG, "device name never arrived, assuming the default variant");
      this->continue_setup_();
    });
    return;
  }

  ESP_LOGD(TAG, "no device name characteristic, assuming the default variant");
  this->continue_setup_();
}

void XbotHub::apply_name_variant_(const std::string &name) {
  ProtocolVariant v = variant_for_name(name);

  if (v.id != SUPPORTED_VARIANT) {
    // Stop trying. The name will not change between cycles, so retrying every
    // interval only wakes the vehicle and holds a connection slot for nothing.
    this->variant_refused_ = true;
    ESP_LOGE(TAG,
             "'%s' speaks protocol variant %u (address 0x%02X, opcode 0x%02X); this component "
             "only decodes variant %u, so it will not talk to it",
             name.c_str(), v.id, v.read_dest, v.read_cmd, SUPPORTED_VARIANT);
    // In the frontend too; the log line is the only other sign, and it scrolls.
    this->status_set_error(LOG_STR("unsupported protocol variant"));
    this->disconnect_();
    return;
  }

  this->xor_key_ = this->key_override_ >= 0 ? static_cast<uint8_t>(this->key_override_) : v.xor_key;
  this->variant_known_ = true;
  ESP_LOGI(TAG, "'%s' -> protocol variant %u, %s", name.c_str(), v.id,
           v.xor_key == 0 ? "frames in the clear" : "frames obfuscated");
  this->continue_setup_();
}

void XbotHub::continue_setup_() {
  this->cancel_timeout("name_read");
  if (this->state_ != State::DISCOVERING) return;

  if (this->write_handle_ == 0) {
    ESP_LOGW(TAG, "no write handle, nothing could be asked of this vehicle");
    this->disconnect_();
    return;
  }

  if (!this->register_for_notify_()) {
    this->disconnect_();
    return;
  }

  this->state_ = State::LISTENING;

  // Probing starts once the subscription is confirmed; this only covers the
  // case where that confirmation never arrives.
  this->set_timeout("cccd_wait", CCCD_WAIT_MS, [this]() {
    if (!this->probes_started_) ESP_LOGW(TAG, "no CCCD confirmation, probing anyway");
    this->start_probes_();
  });

  ESP_LOGI(TAG, "holding the link open");
}

bool XbotHub::resolve_notify_handles_(const TransportProfile &p) {
  auto service = espbt::ESPBTUUID::from_raw(p.service_uuid);
  auto chr = espbt::ESPBTUUID::from_raw(p.notify_uuid);
  auto *characteristic = this->parent()->get_characteristic(service, chr);
  if (characteristic == nullptr) {
    ESP_LOGW(TAG, "notify characteristic missing after profile match");
    return false;
  }
  this->notify_handle_ = characteristic->handle;

  for (const CccdEntry &c : this->cccd_map_) {
    if (c.char_handle == this->notify_handle_) {
      this->cccd_handle_ = c.cccd_handle;
      break;
    }
  }
  if (this->cccd_handle_ == 0) {
    auto *descr = this->parent()->get_config_descriptor(this->notify_handle_);
    if (descr != nullptr) this->cccd_handle_ = descr->handle;
  }
  if (this->cccd_handle_ == 0) {
    ESP_LOGW(TAG, "no CCCD found for notify handle 0x%04X; the device will stay silent",
             this->notify_handle_);
  }
  return true;
}

bool XbotHub::register_for_notify_() {
  esp_err_t err = esp_ble_gattc_register_for_notify(
      static_cast<esp_gatt_if_t>(this->parent()->get_gattc_if()), this->parent()->get_remote_bda(), this->notify_handle_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "register for notify failed, err=%d", err);
    return false;
  }
  return true;
}

void XbotHub::write_cccd_() {
  if (this->cccd_handle_ == 0) {
    this->start_probes_();
    return;
  }
  uint8_t value[2] = {0x01, 0x00};
  esp_err_t err = esp_ble_gattc_write_char_descr(
      static_cast<esp_gatt_if_t>(this->parent()->get_gattc_if()), this->parent()->get_conn_id(), this->cccd_handle_,
      sizeof(value), value, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CCCD write call failed, err=%d", err);
    this->start_probes_();
  }
}

bool XbotHub::resolve_write_handle_(const TransportProfile &p) {
  auto *chr = this->parent()->get_characteristic(espbt::ESPBTUUID::from_raw(p.service_uuid),
                                                 espbt::ESPBTUUID::from_raw(p.write_uuid));
  if (chr == nullptr) {
    ESP_LOGW(TAG, "write characteristic missing");
    return false;
  }
  this->write_handle_ = chr->handle;
  return true;
}

void XbotHub::start_probes_() {
  if (this->probes_started_) return;
  this->probes_started_ = true;
  this->cancel_timeout("cccd_wait");
  if (this->write_handle_ == 0) return;
  if (this->poll_table_.empty()) {
    ESP_LOGW(TAG, "no registers to read, the poll table arrived empty");
    return;
  }
  this->sweep_active_ = true;

  ESP_LOGI(TAG, "reading %u registers x%u, %" PRIu32 " ms apart, write-%s",
           static_cast<unsigned>(this->poll_table_.size()), this->probe_repeat_, PROBE_INTERVAL_MS,
           WRITE_WITH_RESPONSE ? "with-response" : "no-response");
  this->set_timeout("probe", POST_CONNECT_SETTLE_MS, [this]() { this->send_probe_(0, 0); });
}

void XbotHub::send_probe_(size_t index, uint8_t repeat) {
  if (index >= this->poll_table_.size()) {
    this->sweep_active_ = false;
    ESP_LOGI(TAG, "probe list exhausted (notifies=%" PRIu32 ", frames=%" PRIu32 ")",
             this->notifies_this_cycle_, this->frames_this_cycle_);
    return;
  }
  this->pending_probe_index_ = index;
  this->pending_probe_repeat_ = repeat;
  if (this->link_congested_ && this->congestion_waits_ < MAX_CONGESTION_WAITS) {
    // Hold the slot; a full buffer drops the write. The uncongest event resumes
    // from here, and the timer is the fallback if it never arrives.
    this->probe_deferred_ = true;
    this->congestion_waits_++;
    ESP_LOGD(TAG, "probe %u.%u deferred, link congested", static_cast<unsigned>(index), repeat);
    this->set_timeout("probe", PROBE_INTERVAL_MS,
                      [this, index, repeat]() { this->send_probe_(index, repeat); });
    return;
  }
  if (this->link_congested_) {
    // Bounded: an uncongest event that never arrives would hold the sweep for
    // the life of the link, and the watchdog does not fire on a held one.
    ESP_LOGW(TAG, "still congested after %u waits, sending anyway", MAX_CONGESTION_WAITS);
  }
  this->congestion_waits_ = 0;
  this->probe_deferred_ = false;

  const PollEntry &pr = this->poll_table_[index];
  std::array<uint8_t, MAX_FRAME> buf{};
  const uint8_t payload[] = {pr.count};
  size_t len = build_request(buf, pr.dest, pr.cmd, pr.reg, payload);
  std::span<uint8_t> frame(buf.data(), len);
  apply_xor(frame, this->xor_key_);
  ESP_LOGV(TAG, ">>> probe %u.%u dest=0x%02X cmd=0x%02X reg=0x%02X n=0x%02X: %s",
           static_cast<unsigned>(index), repeat, pr.dest, pr.cmd, pr.reg, pr.count,
           hex_dump(frame.data(), frame.size()).c_str());
  esp_err_t err = esp_ble_gattc_write_char(
      static_cast<esp_gatt_if_t>(this->parent()->get_gattc_if()), this->parent()->get_conn_id(), this->write_handle_,
      static_cast<uint16_t>(frame.size()), frame.data(),
      WRITE_WITH_RESPONSE ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
      ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) ESP_LOGW(TAG, "probe write failed, err=%d", err);

  size_t next_index = index;
  uint8_t next_repeat = repeat + 1;
  if (next_repeat >= this->probe_repeat_) {
    next_index = index + 1;
    next_repeat = 0;
  }
  this->set_timeout("probe", PROBE_INTERVAL_MS,
                    [this, next_index, next_repeat]() {
                      this->send_probe_(next_index, next_repeat);
                    });
}

void XbotHub::mark_for_verify_(uint8_t dest, uint8_t reg) {
  size_t limit = std::min<size_t>(this->poll_table_.size(), 32);
  for (size_t i = 0; i < limit; i++) {
    const PollEntry &pr = this->poll_table_[i];
    if (pr.dest != dest) continue;
    int last = pr.reg + pr.count / 2 - 1;
    if (reg < pr.reg || reg > last) continue;
    this->verify_mask_ |= 1u << i;
    // Re-armed, so a burst is read back once after its last write.
    this->set_timeout("verify", WRITE_VERIFY_MS, [this]() { this->run_verify_(); });
    return;
  }
  ESP_LOGW(TAG, "reg=0x%02X was written but no polled block covers it, so nothing reads it back",
           reg);
}

void XbotHub::run_verify_() {
  if (this->state_ != State::LISTENING || this->write_handle_ == 0) {
    this->verify_mask_ = 0;
    return;
  }
  if (this->verify_mask_ == 0) return;
  if (this->link_congested_) {
    ESP_LOGD(TAG, "verify reads held, link congested");
    this->set_timeout("verify", PROBE_INTERVAL_MS, [this]() { this->run_verify_(); });
    return;
  }

  size_t index = static_cast<size_t>(__builtin_ctz(this->verify_mask_));
  this->verify_mask_ &= ~(1u << index);
  const PollEntry &pr = this->poll_table_[index];

  std::array<uint8_t, MAX_FRAME> buf{};
  const uint8_t payload[] = {pr.count};
  size_t len = build_request(buf, pr.dest, pr.cmd, pr.reg, payload);
  std::span<uint8_t> frame(buf.data(), len);
  apply_xor(frame, this->xor_key_);
  esp_err_t err = esp_ble_gattc_write_char(
      static_cast<esp_gatt_if_t>(this->parent()->get_gattc_if()), this->parent()->get_conn_id(),
      this->write_handle_, static_cast<uint16_t>(frame.size()), frame.data(),
      WRITE_WITH_RESPONSE ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
      ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "verify read of block 0x%02X failed to go out, err=%d", pr.reg, err);
  } else {
    ESP_LOGD(TAG, "verify read of block 0x%02X sent", pr.reg);
  }

  // Spaced like probes; a burst goes into the buffer that drops writes silently.
  if (this->verify_mask_ != 0) {
    this->set_timeout("verify", PROBE_INTERVAL_MS, [this]() { this->run_verify_(); });
  }
}

uint32_t XbotHub::persist_key(uint8_t src, uint8_t reg) {
  uint32_t key = fnv1_hash("xbot_persist");
  key = fnv1_hash_extend(key, this->parent()->get_address());
  key = fnv1_hash_extend(key, src);
  key = fnv1_hash_extend(key, reg);
  return key;
}

uint32_t XbotHub::flush_persist_() {
  uint32_t saved = 0;
  for (std::function<bool()> &f : this->persist_flush_) {
    if (f()) saved++;
  }
  if (saved != 0) global_preferences->sync();
  return saved;
}

void XbotHub::check_persist_flush_() {
  if (!this->persist_pending_) return;
  if (millis() - this->last_data_ms_ < PERSIST_IDLE_MS) return;
  this->persist_pending_ = false;
  uint32_t saved = this->flush_persist_();
  ESP_LOGI(TAG, "vehicle out of reach, stored %" PRIu32 " values", saved);
}

void XbotHub::on_shutdown() {
  // A planned restart would otherwise throw away everything read since the last
  // flush. Still one write, and only when something is actually unsaved.
  if (this->flush_persist_() != 0) ESP_LOGI(TAG, "stored pending values before shutdown");
}

void XbotHub::dispatch_registers_(std::span<const uint8_t> frame) {
  if (this->reg_watchers_.empty() || frame.size() < 8) return;
  this->last_data_ms_ = millis();
  this->persist_pending_ = true;
  // Never zero, so a watcher that has not seen a value yet cannot look like it
  // shares a frame with one that has.
  if (++this->frame_seq_ == 0) this->frame_seq_ = 1;
  uint8_t src = frame[3];
  decode_registers(frame, [this, src](RegisterValue rv) {
    for (RegWatcher &w : this->reg_watchers_) {
      if (w.src != src || w.reg != rv.reg) continue;
      // Not-available marker for a plain count, but the legitimate value -1 for
      // a register the entity reads as two's complement.
      if (rv.value == 0xFFFF && !w.accepts_sentinel) continue;
      w.cb(rv.value);
    }
  });
}

bool XbotHub::write_register(uint8_t dest, uint8_t cmd, uint8_t reg, int16_t value) {
  if (this->state_ != State::LISTENING || this->write_handle_ == 0) {
    ESP_LOGW(TAG, "refusing write reg=0x%02X: no link", reg);
    this->status_momentary_warning("write");
    return false;
  }
  if (!this->link_ready()) {
    ESP_LOGW(TAG, "refusing write reg=0x%02X: nothing has decoded yet, so the key is unproven",
             reg);
    this->status_momentary_warning("write");
    return false;
  }
  if (this->link_congested_) {
    ESP_LOGW(TAG, "refusing write reg=0x%02X: link congested", reg);
    this->status_momentary_warning("write");
    return false;
  }
  std::array<uint8_t, MAX_FRAME> buf{};
  size_t len = build_write(buf, dest, cmd, reg, value);
  std::span<uint8_t> frame(buf.data(), len);
  apply_xor(frame, this->xor_key_);
  // Kept loud: this is the audit trail for everything sent to the vehicle.
  ESP_LOGW(TAG, "WRITE dest=0x%02X cmd=0x%02X reg=0x%02X value=%d", dest, cmd, reg, value);
  esp_err_t err = esp_ble_gattc_write_char(
      static_cast<esp_gatt_if_t>(this->parent()->get_gattc_if()), this->parent()->get_conn_id(), this->write_handle_,
      static_cast<uint16_t>(frame.size()), frame.data(),
      WRITE_WITH_RESPONSE ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
      ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "write failed, err=%d", err);
    this->status_momentary_warning("write");
    return false;
  }
  // The write went out unacknowledged, so read the register back rather than
  // leaving the entity showing what was asked for until the next sweep, which
  // on the default interval is five minutes away. Whatever the vehicle reports
  // reaches the entity through the same watcher a sweep uses, so a write that
  // did not land corrects itself instead of standing.
  this->mark_for_verify_(dest, reg);
  return true;
}

void XbotHub::handle_notify_(const esp_ble_gattc_cb_param_t::gattc_notify_evt_param &n) {
  if (n.value == nullptr || n.value_len == 0) return;
  // Every node on this ble_client sees every notification, so another entity
  // subscribed to a different characteristic would otherwise feed our decoder.
  if (this->notify_handle_ != 0 && n.handle != this->notify_handle_) return;

  this->notifies_this_cycle_++;

  ESP_LOGV(TAG, "<<< notify h=0x%04X len=%u: %s", n.handle, n.value_len,
           hex_dump(n.value, n.value_len).c_str());

  // Kept raw for the end-of-cycle triage, before the key is applied.
  if (this->first_chunk_len_ == 0) {
    this->first_chunk_len_ = std::min<size_t>(n.value_len, this->first_chunk_.size());
    std::copy(n.value, n.value + this->first_chunk_len_, this->first_chunk_.begin());
  }

  size_t dropped = this->rx_.append(n.value, n.value_len, this->xor_key_);
  if (dropped != 0)
    ESP_LOGW(TAG, "receive buffer never synced, dropping %u bytes", static_cast<unsigned>(dropped));

  this->report_frames_();
}

void XbotHub::report_frames_() {
  this->rx_.drain([this](std::span<const uint8_t> f) {
    this->frames_this_cycle_++;
    ESP_LOGV(TAG, "*** FRAME src=0x%02X cmd=0x%02X reg=0x%02X len=%u: %s", f[3], f[4], f[5],
             static_cast<unsigned>(f.size()), hex_dump(f.data(), f.size()).c_str());
    this->dispatch_registers_(f);
  });
}

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
