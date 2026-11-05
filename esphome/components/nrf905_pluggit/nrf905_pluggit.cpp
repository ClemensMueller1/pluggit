#include "nrf905_pluggit.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace nrf905_pluggit {

static const char *const TAG = "nrf905_pluggit";

// Answer must follow the 0x0400 request within this window.
static const uint32_t SENSOR_BLOCK_WINDOW_MS = 1500;

// Remote read of holding registers 0x0400, 12 registers. The unit answered this
// exact frame. Bytes after 00 0C change between remote sessions and are not decoded.
// Reply layout of 80 90 03 18 (byte offsets in the 32-byte payload):
//   4 stage, 9..10 ShockBurst address low bytes, 20 T1, 21 T2, 22 T3, 23 T4, °C.
//   80 90 03 18 01 .. 2B 9F .. 0A 13 14 0B .. → stage 1, 10/19/20/11 °C
//   80 90 03 18 02 .. 2B 9F .. 0A 13 14 0B .. → stage 2, same temperatures
//   forum 80 90 03 18 02 .. 0A 7A .. 07 10 12 07 .. → 7/16/18/7 °C
static const uint8_t SENSOR_QUERY[NRF_PAYLOAD_LEN] = {
    0x90, 0x80, 0x03, 0x04, 0x00, 0x00, 0x0C, 0x02, 0x00, 0x01, 0xCF, 0x5C, 0xFE, 0x13, 0xF7, 0xFE,
    0x7F, 0x7C, 0xDF, 0x0C, 0xF9, 0x7A, 0xFE, 0xBC, 0x7B, 0xAB, 0xFF, 0xC1, 0xFF, 0xB9, 0x93, 0xFD};

void Nrf905Pluggit::setup() {
  this->ce_pin_->setup();
  this->txe_pin_->setup();
  this->pwr_pin_->setup();
  this->dr_pin_->setup();
  if (this->cd_pin_ != nullptr)
    this->cd_pin_->setup();
  if (this->am_pin_ != nullptr)
    this->am_pin_->setup();

  this->ce_pin_->digital_write(false);
  this->txe_pin_->digital_write(false);
  this->pwr_pin_->digital_write(true);
  delay(5);

  this->spi_setup();
  this->pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("nrf905_pluggit_addr"), true);
  this->sniff_mode_ = false;
  this->load_address_();
  this->write_config_();
  if (!this->verify_config_()) {
    ESP_LOGE(TAG, "SPI readback mismatch — check CSN/MOSI/MISO/SCK and 3.3 V");
    if (this->radio_ok_ != nullptr)
      this->radio_ok_->publish_state(false);
    this->mark_failed();
    return;
  }
  if (this->radio_ok_ != nullptr)
    this->radio_ok_->publish_state(true);
  if (this->rf_rx_ != nullptr)
    this->rf_rx_->publish_state(false);

  this->write_tx_address_();
  this->enter_rx_();

  if (this->has_address_()) {
    ESP_LOGI(TAG, "Replay 868.4 MHz addr %s CRC-16 (sniff only via RF Adresse neu lernen)",
             to_hex_(this->rx_address_, 4).c_str());
    if (this->listen_address_ != nullptr)
      this->listen_address_->publish_state(to_hex_(this->rx_address_, 4));
  } else {
    ESP_LOGW(TAG, "No ShockBurst address yet — press RF Adresse neu lernen, then store Off/1/2/3 packets in HA");
    if (this->listen_address_ != nullptr)
      this->listen_address_->publish_state("unset");
  }

  this->set_interval("nrf905_heartbeat", 15000, [this]() { this->log_heartbeat_(); });
}

void Nrf905Pluggit::dump_config() {
  ESP_LOGCONFIG(TAG, "nRF905 Pluggit (868.4 MHz):");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  CE Pin: ", this->ce_pin_);
  LOG_PIN("  TXE Pin: ", this->txe_pin_);
  LOG_PIN("  PWR Pin: ", this->pwr_pin_);
  LOG_PIN("  DR Pin: ", this->dr_pin_);
  if (this->cd_pin_ != nullptr)
    LOG_PIN("  CD Pin: ", this->cd_pin_);
  if (this->am_pin_ != nullptr)
    LOG_PIN("  AM Pin: ", this->am_pin_);
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->sniff_mode_ ? "sniff (CRC off, TX address width 4, RX address width 0)" : "replay CRC-16");
  ESP_LOGCONFIG(TAG, "  NVM address: %s",
                this->nvm_valid_ ? to_hex_(this->nvm_address_, 4).c_str() : "unset");
  ESP_LOGCONFIG(TAG, "  Address: %s",
                this->has_address_() ? to_hex_(this->rx_address_, 4).c_str() : "unset");
  ESP_LOGCONFIG(TAG, "  Link test: addr 00 00 12 34 CRC-16 cfg 76 0E 44 20 20 00 00 12 34 D8");
}

void Nrf905Pluggit::loop() {
  if (this->is_failed() || this->dr_pin_ == nullptr)
    return;
  if (!this->dr_pin_->digital_read())
    return;
  this->read_payload_();
}

void Nrf905Pluggit::enter_standby_() {
  this->ce_pin_->digital_write(false);
  this->txe_pin_->digital_write(false);
  delay(1);
}

void Nrf905Pluggit::write_config_() {
  // 868.4 MHz: HFREQ_PLL=1, CH_NO=118 (0x76).
  //   f = (422.4 + 11.8) * 2 = 868.4 MHz.
  //   Byte 1 0x0E = PA_PWR=+10 dBm, HFREQ_PLL=1, CH_NO[8]=0.
  // Replay: 0x44 = TX/RX address width 4, CRC-16.
  // Sniff: 0x40 = TX address width 4, RX address width 0, CRC off.
  // The chip sends an address LSB first, so 11 22 33 44 is on air as
  // 44 33 22 11. Width 0 is stored by the config register but this module
  // then clocks the 4-byte TX address out 16 times (64 bytes) before the
  // payload. TX stays at the documented width 4; RX stays 0 so the sender
  // address is not stripped. start_sniff() rejects the mode unless byte 2
  // reads back 0x40.
  uint8_t cfg[10] = {
      0x76,
      0x0E,
      static_cast<uint8_t>(this->sniff_mode_ ? 0x40 : 0x44),
      0x20,
      0x20,
      static_cast<uint8_t>(this->sniff_mode_ ? 0x00 : this->rx_address_[0]),
      static_cast<uint8_t>(this->sniff_mode_ ? 0x00 : this->rx_address_[1]),
      static_cast<uint8_t>(this->sniff_mode_ ? 0x00 : this->rx_address_[2]),
      static_cast<uint8_t>(this->sniff_mode_ ? 0x00 : this->rx_address_[3]),
      static_cast<uint8_t>(this->sniff_mode_ ? 0x18 : 0xD8),
  };
  memcpy(this->last_cfg_, cfg, sizeof(cfg));

  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_WC);
  this->write_array(cfg, sizeof(cfg));
  this->disable();
}

void Nrf905Pluggit::write_tx_address_bytes_(const uint8_t *addr) {
  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_WTA);
  this->write_array(addr, 4);
  this->disable();
}

void Nrf905Pluggit::write_tx_address_() { this->write_tx_address_bytes_(this->rx_address_); }

// One register image for both test radios. RX address bytes and the TX
// address written afterwards are the same array, so the two modules cannot
// disagree on address width, CRC, channel, or payload length.
static const uint8_t TEST_CFG[10] = {0x76, 0x0E, 0x44, 0x20, 0x20, 0x00, 0x00, 0x12, 0x34, 0xD8};

bool Nrf905Pluggit::program_test_radio_() {
  if (this->is_failed()) {
    ESP_LOGW(TAG, "TEST skipped: nRF905 setup failed");
    return false;
  }
  if (this->sniff_mode_) {
    ESP_LOGW(TAG, "TEST skipped: sniff is active — set the ShockBurst address in Home Assistant to leave sniff");
    return false;
  }
  this->sniff_mode_ = false;
  memcpy(this->last_cfg_, TEST_CFG, sizeof(TEST_CFG));

  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_WC);
  this->write_array(TEST_CFG, sizeof(TEST_CFG));
  this->disable();
  if (!this->verify_config_()) {
    this->test_mode_ = false;
    ESP_LOGE(TAG, "TEST config readback mismatch — check SPI, both modules must show cfg 76 0E 44 20 20 00 00 12 34 D8");
    return false;
  }
  this->write_tx_address_bytes_(TEST_CFG + 5);
  this->test_mode_ = true;
  ESP_LOGI(TAG, "TEST radio cfg %s (868.4 MHz, addr 00 00 12 34, AFW 4/4, PW 32, CRC-16)",
           to_hex_(TEST_CFG, sizeof(TEST_CFG)).c_str());
  return true;
}

void Nrf905Pluggit::leave_test_() {
  this->test_mode_ = false;
  this->sniff_mode_ = false;
  this->write_config_();
  this->write_tx_address_();
  this->enter_rx_();
  const std::string shown = this->has_address_() ? to_hex_(this->rx_address_, 4) : std::string("unset");
  ESP_LOGI(TAG, "TEST off — restored replay addr %s CRC-16", shown.c_str());
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state(shown);
}

void Nrf905Pluggit::fill_test_payload_(const std::string &message, uint8_t *out) {
  memset(out, 0, NRF_PAYLOAD_LEN);
  size_t begin = 0;
  size_t end = message.size();
  while (begin < end && (message[begin] == ' ' || message[begin] == '\t' || message[begin] == '\r' ||
                         message[begin] == '\n'))
    begin++;
  while (end > begin && (message[end - 1] == ' ' || message[end - 1] == '\t' || message[end - 1] == '\r' ||
                         message[end - 1] == '\n'))
    end--;
  if (begin == end) {
    static const char PATTERN[] = "PLUGGIT NRF905 LINK TEST";
    memcpy(out, PATTERN, sizeof(PATTERN) - 1);
    return;
  }

  bool hex_only = true;
  size_t hex_digits = 0;
  for (size_t i = begin; i < end; i++) {
    const char c = message[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    const bool sep = c == ' ' || c == ':' || c == '-';
    if (hex)
      hex_digits++;
    else if (!sep)
      hex_only = false;
  }
  if (hex_only && hex_digits >= 2 && (hex_digits % 2) == 0 && hex_digits <= NRF_PAYLOAD_LEN * 2) {
    parse_hex_(message.substr(begin, end - begin), out, NRF_PAYLOAD_LEN);
    return;
  }
  const size_t n = end - begin > NRF_PAYLOAD_LEN ? NRF_PAYLOAD_LEN : end - begin;
  memcpy(out, message.data() + begin, n);
}

bool Nrf905Pluggit::verify_config_() {
  uint8_t got[10] = {0};
  this->enter_standby_();
  this->enable();
  const uint8_t status = this->transfer_byte(NRF_CMD_RC);
  this->read_array(got, sizeof(got));
  this->disable();

  ESP_LOGD(TAG, "SPI status 0x%02X (AM=%d DR=%d)", status, !!(status & NRF_STATUS_AM), !!(status & NRF_STATUS_DR));
  ESP_LOGD(TAG, "CFG wrote %s", to_hex_(this->last_cfg_, 10).c_str());
  ESP_LOGD(TAG, "CFG read  %s", to_hex_(got, 10).c_str());

  if (memcmp(got, this->last_cfg_, sizeof(got)) != 0) {
    ESP_LOGE(TAG, "CFG mismatch wrote %s", to_hex_(this->last_cfg_, 10).c_str());
    ESP_LOGE(TAG, "CFG mismatch read  %s", to_hex_(got, 10).c_str());
    return false;
  }
  return true;
}

void Nrf905Pluggit::enter_rx_() {
  this->txe_pin_->digital_write(false);
  this->ce_pin_->digital_write(true);
  delay(1);
}

bool Nrf905Pluggit::has_address_() const {
  return this->rx_address_[0] != 0 || this->rx_address_[1] != 0 || this->rx_address_[2] != 0 ||
         this->rx_address_[3] != 0;
}

void Nrf905Pluggit::save_address_() {
  uint32_t packed = static_cast<uint32_t>(this->rx_address_[0]) | (static_cast<uint32_t>(this->rx_address_[1]) << 8) |
                    (static_cast<uint32_t>(this->rx_address_[2]) << 16) | (static_cast<uint32_t>(this->rx_address_[3]) << 24);
  if (!this->pref_.save(&packed)) {
    ESP_LOGW(TAG, "NVM ShockBurst address save failed");
    return;
  }
  memcpy(this->nvm_address_, this->rx_address_, 4);
  this->nvm_valid_ = true;
}

void Nrf905Pluggit::load_address_() {
  uint32_t packed = 0;
  if (!this->pref_.load(&packed) || packed == 0) {
    this->nvm_valid_ = false;
    ESP_LOGI(TAG, "NVM ShockBurst address: unset");
    return;
  }
  this->nvm_address_[0] = packed & 0xFF;
  this->nvm_address_[1] = (packed >> 8) & 0xFF;
  this->nvm_address_[2] = (packed >> 16) & 0xFF;
  this->nvm_address_[3] = (packed >> 24) & 0xFF;
  this->nvm_valid_ = true;
  const std::string nvm = to_hex_(this->nvm_address_, 4);
  ESP_LOGI(TAG, "NVM ShockBurst address: %s", nvm.c_str());
  if (this->address_from_yaml_) {
    ESP_LOGI(TAG, "YAML rx_address %s overrides NVM", to_hex_(this->rx_address_, 4).c_str());
    return;
  }
  memcpy(this->rx_address_, this->nvm_address_, 4);
}

void Nrf905Pluggit::stop_sniff_() {
  this->sniff_mode_ = false;
  if (this->nvm_valid_)
    memcpy(this->rx_address_, this->nvm_address_, 4);
  else
    memset(this->rx_address_, 0, 4);
  this->write_config_();
  this->write_tx_address_();
  this->enter_rx_();
  const std::string shown = this->has_address_() ? to_hex_(this->rx_address_, 4) : std::string("unset");
  ESP_LOGW(TAG, "SNIFF stopped. Replay addr %s", shown.c_str());
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state(shown);
}

void Nrf905Pluggit::note_sniff_frame_(const uint8_t *buf) {
  // RX address width 0 does not strip the sender's address. Pluggit still
  // sends four address bytes, LSB first: 00 00 2B 9F is on air as
  // 9F 2B 00 00, then the payload 80 90 03 18. A frame with no address
  // prefix can start at offset 0. Noise is not logged: only a buffer that
  // contains 80 90, or every 100th frame.
  this->sniff_rx_count_++;
  bool has_pluggit = false;
  for (size_t i = 0; i + 1 < NRF_PAYLOAD_LEN; i++) {
    if (buf[i] == 0x80 && buf[i + 1] == 0x90) {
      has_pluggit = true;
      break;
    }
  }
  const bool sample = (this->sniff_rx_count_ % 100) == 0;
  if (has_pluggit) {
    ESP_LOGI(TAG, "SNIFF RX %s", to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
  } else if (sample) {
    ESP_LOGI(TAG, "SNIFF RX #%u %s", this->sniff_rx_count_, to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
  }
  for (size_t off = 0; off + 10 < NRF_PAYLOAD_LEN; off++) {
    if (buf[off] != 0x80 || buf[off + 1] != 0x90 || buf[off + 2] != 0x03 || buf[off + 3] != 0x18)
      continue;
    // Address 05/06 are payload bytes 9 and 10 of the 80 90 frame.
    const uint8_t addr5 = buf[off + 9];
    const uint8_t addr6 = buf[off + 10];
    ESP_LOGI(TAG, "SNIFF device address 00 00 %02X %02X", addr5, addr6);
    return;
  }
}

void Nrf905Pluggit::read_payload_() {
  uint8_t buf[NRF_PAYLOAD_LEN];
  this->enter_standby_();
  this->enable();
  const uint8_t status = this->transfer_byte(NRF_CMD_RRP);
  this->read_array(buf, NRF_PAYLOAD_LEN);
  this->disable();
  this->enter_rx_();
  if ((status & NRF_STATUS_DR) == 0) {
    return;
  }

  if (this->test_mode_) {
    // CRC-16 is on, so DR only fires when the frame matched address and CRC.
    ESP_LOGI(TAG, "TEST RX CRC-16 OK %s", to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
    this->publish_packet_(buf, NRF_PAYLOAD_LEN);
    return;
  }

  if (this->sniff_mode_) {
    this->note_sniff_frame_(buf);
    return;
  }

  this->publish_packet_(buf, NRF_PAYLOAD_LEN);
}

void Nrf905Pluggit::publish_packet_(const uint8_t *buf, size_t len) {
  const std::string hex = this->to_hex_(buf, len);
  ESP_LOGI(TAG, "RX %s", hex.c_str());
  if (this->last_packet_ != nullptr)
    this->last_packet_->publish_state(hex);
  if (this->rf_rx_ != nullptr)
    this->rf_rx_->publish_state(true);
  for (auto *trigger : this->on_packet_)
    trigger->trigger(hex);
  this->decode_pluggit_frame_(buf);
}

void Nrf905Pluggit::start_sniff() {
  if (this->is_failed()) {
    ESP_LOGW(TAG, "SNIFF skipped: nRF905 setup failed");
    return;
  }
  this->test_mode_ = false;
  this->sniff_mode_ = true;
  this->sniff_rx_count_ = 0;
  this->write_config_();
  const bool cfg_ok = this->verify_config_();
  ESP_LOGI(TAG, "SNIFF cfg %s (868.4 MHz, TX address width 4, RX address width 0, CRC off)",
           to_hex_(this->last_cfg_, 10).c_str());
  if (!cfg_ok || this->last_cfg_[2] != 0x40) {
    ESP_LOGE(TAG, "Chip rejected sniff address widths (config byte 2 is not 0x40)");
    this->stop_sniff_();
    return;
  }
  uint8_t test[NRF_PAYLOAD_LEN] = {0};
  fill_test_payload_("", test);
  ESP_LOGI(TAG, "SNIFF sending test payload once");
  this->transmit_bytes_(test);
  ESP_LOGI(TAG, "SNIFF listening. Set the ShockBurst address in Home Assistant to leave sniff.");
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state("sniff");
  if (this->rf_rx_ != nullptr)
    this->rf_rx_->publish_state(false);
}

void Nrf905Pluggit::set_address_hex(const std::string &hex) {
  if (this->is_failed()) {
    ESP_LOGW(TAG, "ShockBurst address not set: nRF905 setup failed");
    return;
  }
  uint8_t buf[4] = {0};
  const size_t n = this->parse_hex_(hex, buf, 4);
  if (n != 4 || (buf[0] | buf[1] | buf[2] | buf[3]) == 0) {
    ESP_LOGW(TAG, "ShockBurst address rejected (need 4 non-zero bytes): '%s'", hex.c_str());
    return;
  }
  if (!this->test_mode_ && !this->sniff_mode_ && memcmp(buf, this->rx_address_, 4) == 0) {
    const std::string pretty = to_hex_(buf, 4);
    ESP_LOGD(TAG, "ShockBurst address unchanged %s", pretty.c_str());
    if (this->listen_address_ != nullptr)
      this->listen_address_->publish_state(pretty);
    return;
  }
  const bool leaving_sniff = this->sniff_mode_;
  memcpy(this->rx_address_, buf, 4);
  this->test_mode_ = false;
  this->sniff_mode_ = false;
  this->save_address_();
  this->write_config_();
  this->write_tx_address_();
  this->enter_rx_();
  const std::string pretty = to_hex_(this->rx_address_, 4);
  if (leaving_sniff)
    ESP_LOGI(TAG, "SNIFF off — address set from Home Assistant");
  ESP_LOGI(TAG, "ShockBurst address set to %s and saved to NVM", pretty.c_str());
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state(pretty);
}

void Nrf905Pluggit::transmit_bytes_(const uint8_t *buf) {
  const bool resume_sniff = this->sniff_mode_;
  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_WTP);
  this->write_array(buf, NRF_PAYLOAD_LEN);
  this->disable();
  this->write_tx_address_();

  this->txe_pin_->digital_write(true);
  this->ce_pin_->digital_write(true);
  delay(20);
  if (resume_sniff) {
    this->sniff_mode_ = true;
    this->write_config_();
  }
  this->enter_rx_();
  ESP_LOGI(TAG, "TX %s", this->to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
}

void Nrf905Pluggit::transmit_hex(const std::string &hex) {
  if (this->test_mode_)
    this->leave_test_();
  uint8_t buf[NRF_PAYLOAD_LEN] = {0};
  this->parse_hex_(hex, buf, NRF_PAYLOAD_LEN);

  if (!this->has_address_()) {
    ESP_LOGW(TAG, "TX skipped: no ShockBurst address — press RF Adresse neu lernen");
    return;
  }
  this->transmit_bytes_(buf);
}

void Nrf905Pluggit::request_sensors() {
  if (this->is_failed()) {
    ESP_LOGW(TAG, "Sensor poll skipped: nRF905 setup failed");
    return;
  }
  if (this->sniff_mode_ || this->test_mode_) {
    ESP_LOGW(TAG, "Sensor poll skipped: sniff or link test is active");
    return;
  }
  if (!this->has_address_()) {
    ESP_LOGW(TAG, "Sensor poll skipped: no ShockBurst address — press RF Adresse neu lernen");
    return;
  }
  // Our own TX is not received, so arm the 0x0400 window before the frame goes out.
  this->expect_block_ = ExpectBlock::BLOCK_0400;
  this->expect_at_ms_ = millis();
  this->transmit_bytes_(SENSOR_QUERY);
}

void Nrf905Pluggit::publish_fan_stage_(uint8_t stage) {
  if (this->fan_stage_sensor_ != nullptr)
    this->fan_stage_sensor_->publish_state(stage);
}

void Nrf905Pluggit::publish_temperature_(sensor::Sensor *sens, uint8_t celsius, const char *name) {
  if (celsius > 80) {
    ESP_LOGW(TAG, "%s %u C ignored", name, celsius);
    return;
  }
  if (sens != nullptr)
    sens->publish_state(celsius);
}

void Nrf905Pluggit::decode_pluggit_frame_(const uint8_t *buf) {
  if (this->test_mode_ || this->sniff_mode_)
    return;

  if (buf[0] == 0x90 && buf[1] == 0x80) {
    const uint8_t fc = buf[2];
    const uint16_t reg = (static_cast<uint16_t>(buf[3]) << 8) | buf[4];
    const uint16_t count = (static_cast<uint16_t>(buf[5]) << 8) | buf[6];
    if (fc == 0x03) {
      this->expect_block_ = (reg == 0x0400 && count == 0x000C) ? ExpectBlock::BLOCK_0400 : ExpectBlock::OTHER;
      this->expect_at_ms_ = millis();
      return;
    }
    // 90 80 10 00 00 00 01 02 00 SS — write fan stage, low byte only.
    if (fc == 0x10 && reg == 0x0000 && count == 0x0001 && buf[7] == 0x02 && buf[8] == 0x00 && buf[9] <= 3) {
      this->publish_fan_stage_(buf[9]);
      ESP_LOGI(TAG, "Fan stage %u from 90 80 write", buf[9]);
    }
    return;
  }

  if (buf[0] != 0x80 || buf[1] != 0x90 || buf[2] != 0x03)
    return;

  const bool sensor_block = this->expect_block_ == ExpectBlock::BLOCK_0400 && buf[3] == 0x18 &&
                            (millis() - this->expect_at_ms_) < SENSOR_BLOCK_WINDOW_MS;
  // Any FC03 answer consumes the pending request, including 0x040C.
  this->expect_block_ = ExpectBlock::NONE;
  if (!sensor_block)
    return;

  if (!this->has_address_() || buf[9] != this->rx_address_[2] || buf[10] != this->rx_address_[3]) {
    ESP_LOGW(TAG, "Sensor block ignored: payload address %02X %02X != %s", buf[9], buf[10],
             this->has_address_() ? to_hex_(this->rx_address_ + 2, 2).c_str() : "unset");
    return;
  }

  const uint8_t stage = buf[4];
  const uint8_t t1 = buf[20];
  const uint8_t t2 = buf[21];
  const uint8_t t3 = buf[22];
  const uint8_t t4 = buf[23];
  ESP_LOGI(TAG, "Sensors stage=%u T1=%u T2=%u T3=%u T4=%u", stage, t1, t2, t3, t4);
  if (stage <= 3)
    this->publish_fan_stage_(stage);
  else
    ESP_LOGW(TAG, "Fan stage %u ignored", stage);
  this->publish_temperature_(this->t1_sensor_, t1, "T1");
  this->publish_temperature_(this->t2_sensor_, t2, "T2");
  this->publish_temperature_(this->t3_sensor_, t3, "T3");
  this->publish_temperature_(this->t4_sensor_, t4, "T4");
}

void Nrf905Pluggit::test_listen(const std::string &command) {
  std::string cmd;
  cmd.reserve(command.size());
  for (char c : command) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      continue;
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
    cmd.push_back(c);
  }
  if (cmd == "0" || cmd == "off" || cmd == "normal" || cmd == "stop" || cmd == "exit") {
    if (this->sniff_mode_) {
      ESP_LOGW(TAG, "TEST off ignored: sniff is active — set the ShockBurst address in Home Assistant to leave sniff");
      return;
    }
    this->leave_test_();
    return;
  }
  if (!this->program_test_radio_())
    return;
  this->enter_rx_();
  ESP_LOGI(TAG, "TEST listen on rx/test — other module publishes tx/test (addr 00 00 12 34, CRC-16)");
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state("00 00 12 34");
  if (this->rf_rx_ != nullptr)
    this->rf_rx_->publish_state(false);
}

void Nrf905Pluggit::transmit_test(const std::string &message) {
  uint8_t buf[NRF_PAYLOAD_LEN] = {0};
  fill_test_payload_(message, buf);
  if (!this->program_test_radio_())
    return;

  // A few copies so a receiver that has just switched config still catches one.
  for (int i = 0; i < 90; i++) {
    this->enter_standby_();
    this->enable();
    this->write_byte(NRF_CMD_WTP);
    this->write_array(buf, NRF_PAYLOAD_LEN);
    this->disable();
    this->write_tx_address_bytes_(TEST_CFG + 5);
    this->txe_pin_->digital_write(true);
    this->ce_pin_->digital_write(true);
    delay(20);
    this->enter_rx_();
    if (i + 1 < 90)
      delay(40);
  }
  ESP_LOGI(TAG, "TEST TX %s", to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
}

void Nrf905Pluggit::log_heartbeat_() {
  if (this->is_failed())
    return;
  const int dr = this->dr_pin_ != nullptr && this->dr_pin_->digital_read();
  const int cd = this->cd_pin_ == nullptr ? -1 : this->cd_pin_->digital_read();
  const int am = this->am_pin_ == nullptr ? -1 : this->am_pin_->digital_read();
  if (this->test_mode_) {
    ESP_LOGD(TAG, "TEST listen 868.4 MHz addr 00 00 12 34 CRC-16 DR=%d CD=%d AM=%d", dr, cd, am);
  } else if (this->sniff_mode_) {
    ESP_LOGD(TAG, "sniff 868.4 MHz CRC off TX address width 4 RX address width 0 DR=%d CD=%d AM=%d", dr, cd, am);
  } else {
    ESP_LOGD(TAG, "replay 868.4 MHz addr=%s DR=%d CD=%d AM=%d",
             this->has_address_() ? to_hex_(this->rx_address_, 4).c_str() : "unset", dr, cd, am);
  }
}

std::string Nrf905Pluggit::to_hex_(const uint8_t *data, size_t len) {
  static const char *const DIGITS = "0123456789ABCDEF";
  std::string out;
  out.reserve(len * 3);
  for (size_t i = 0; i < len; i++) {
    if (i != 0)
      out.push_back(' ');
    out.push_back(DIGITS[data[i] >> 4]);
    out.push_back(DIGITS[data[i] & 0x0F]);
  }
  return out;
}

size_t Nrf905Pluggit::parse_hex_(const std::string &hex, uint8_t *out, size_t max_len) {
  size_t n = 0;
  int nibble = -1;
  for (char c : hex) {
    int v = -1;
    if (c >= '0' && c <= '9')
      v = c - '0';
    else if (c >= 'A' && c <= 'F')
      v = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
      v = c - 'a' + 10;
    if (v < 0)
      continue;
    if (nibble < 0) {
      nibble = v;
    } else {
      if (n < max_len)
        out[n++] = static_cast<uint8_t>((nibble << 4) | v);
      nibble = -1;
    }
  }
  return n;
}

}  // namespace nrf905_pluggit
}  // namespace esphome
