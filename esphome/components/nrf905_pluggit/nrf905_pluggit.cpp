#include "nrf905_pluggit.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace nrf905_pluggit {

static const char *const TAG = "nrf905_pluggit";

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

  if (this->sniff_mode_) {
    ESP_LOGI(TAG, "Sniffing 868.4 MHz (1-byte addr 0x00, CRC off). Press a key on the remote within ~20 cm");
    if (this->listen_address_ != nullptr)
      this->listen_address_->publish_state("sniff");
  } else {
    ESP_LOGI(TAG, "Listening 868.4 MHz addr %s CRC-16", to_hex_(this->rx_address_, 4).c_str());
    if (this->listen_address_ != nullptr)
      this->listen_address_->publish_state(to_hex_(this->rx_address_, 4));
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
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->sniff_mode_ ? "sniff (CRC off, addr[0]=0x00)" : "locked CRC-16");
  if (!this->sniff_mode_)
    ESP_LOGCONFIG(TAG, "  Address: %s", to_hex_(this->rx_address_, 4).c_str());
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
  // Byte 0 CH_NO=0x76 (118). Byte 1 MUST have HFREQ_PLL=1 for 868 MHz:
  //   f = (422.4 + 11.8) * 2 = 868.4 MHz. 0x0C is 434.2 MHz (HFREQ_PLL=0).
  //   0x0E = PA_PWR=+10 dBm, HFREQ_PLL=1, CH_NO[8]=0.
  uint8_t cfg[10] = {
      0x76, 0x0E, 0x44, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0xD8,
  };
  if (this->sniff_mode_) {
    // Pluggit ShockBurst address is 00 00 xx xx (KNX forum #218). Match the
    // first byte, CRC off, so the remaining address lands in the payload.
    cfg[2] = 0x41;  // TX_AFW=4, RX_AFW=1
    cfg[5] = 0x00;
    cfg[9] = 0x18;  // 16 MHz crystal, CRC disabled
  } else {
    cfg[2] = 0x44;  // TX_AFW=4, RX_AFW=4
    cfg[5] = this->rx_address_[0];
    cfg[6] = this->rx_address_[1];
    cfg[7] = this->rx_address_[2];
    cfg[8] = this->rx_address_[3];
    cfg[9] = 0xD8;  // 16 MHz crystal, CRC-16
  }
  memcpy(this->last_cfg_, cfg, sizeof(cfg));

  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_WC);
  this->write_array(cfg, sizeof(cfg));
  this->disable();
}

void Nrf905Pluggit::write_tx_address_() {
  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_WTA);
  this->write_array(this->rx_address_, 4);
  this->disable();
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

bool Nrf905Pluggit::looks_like_pluggit_(const uint8_t *buf) const {
  // After matching address byte 0 = 0x00, payload starts with address[1..3]:
  // 00 xx xx | payload. Pluggit uses 00 00 xx xx; payload often 90/80/88.
  if (buf[0] != 0x00)
    return false;
  const uint8_t p0 = buf[3];
  return p0 == 0x90 || p0 == 0x80 || p0 == 0x88;
}

void Nrf905Pluggit::lock_address_(const uint8_t *sniff_buf) {
  this->rx_address_[0] = 0x00;
  this->rx_address_[1] = sniff_buf[0];
  this->rx_address_[2] = sniff_buf[1];
  this->rx_address_[3] = sniff_buf[2];
  this->sniff_mode_ = false;
  this->write_config_();
  this->write_tx_address_();
  this->enter_rx_();
  const std::string addr = to_hex_(this->rx_address_, 4);
  ESP_LOGI(TAG, "Locked ShockBurst address %s (CRC-16). Press the remote again for a full 32-byte frame", addr.c_str());
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state(addr);
}

void Nrf905Pluggit::read_payload_() {
  uint8_t buf[NRF_PAYLOAD_LEN];
  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_RRP);
  this->read_array(buf, NRF_PAYLOAD_LEN);
  this->disable();
  this->enter_rx_();

  if (this->sniff_mode_) {
    if (!this->looks_like_pluggit_(buf)) {
      const uint32_t now = millis();
      if (now - this->last_noise_ms_ > 1000) {
        this->last_noise_ms_ = now;
        ESP_LOGD(TAG, "RX noise %s", to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
      }
      return;
    }
    this->publish_packet_(buf, NRF_PAYLOAD_LEN);
    this->lock_address_(buf);
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
}

void Nrf905Pluggit::start_sniff() {
  this->sniff_mode_ = true;
  this->write_config_();
  this->enter_rx_();
  ESP_LOGI(TAG, "Sniff mode — press a key on the remote within ~20 cm");
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state("sniff");
  if (this->rf_rx_ != nullptr)
    this->rf_rx_->publish_state(false);
}

void Nrf905Pluggit::transmit_hex(const std::string &hex) {
  uint8_t buf[NRF_PAYLOAD_LEN] = {0};
  this->parse_hex_(hex, buf, NRF_PAYLOAD_LEN);

  if (this->sniff_mode_) {
    ESP_LOGW(TAG, "TX skipped: ShockBurst address not locked yet");
    return;
  }

  this->enter_standby_();
  this->enable();
  this->write_byte(NRF_CMD_WTP);
  this->write_array(buf, NRF_PAYLOAD_LEN);
  this->disable();
  this->write_tx_address_();

  this->txe_pin_->digital_write(true);
  this->ce_pin_->digital_write(true);
  delay(20);
  this->enter_rx_();
  ESP_LOGI(TAG, "TX %s", this->to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
}

void Nrf905Pluggit::log_heartbeat_() {
  if (this->is_failed())
    return;
  const int dr = this->dr_pin_ != nullptr && this->dr_pin_->digital_read();
  const int cd = this->cd_pin_ == nullptr ? -1 : this->cd_pin_->digital_read();
  const int am = this->am_pin_ == nullptr ? -1 : this->am_pin_->digital_read();
  ESP_LOGD(TAG, "waiting 868.4 MHz sniff=%d DR=%d CD=%d AM=%d — press remote, keep module ~20 cm away",
           this->sniff_mode_, dr, cd, am);
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
