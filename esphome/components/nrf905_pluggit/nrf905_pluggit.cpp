#include "nrf905_pluggit.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace nrf905_pluggit {

static const char *const TAG = "nrf905_pluggit";

void Nrf905Pluggit::setup() {
  this->ce_pin_->setup();
  this->txe_pin_->setup();
  this->pwr_pin_->setup();
  this->dr_pin_->setup();

  this->ce_pin_->digital_write(false);
  this->txe_pin_->digital_write(false);
  this->pwr_pin_->digital_write(true);
  delay(5);

  this->spi_setup();
  this->write_config_();
  this->enter_rx_();
  ESP_LOGI(TAG, "Listening on 868.4 MHz (nRF905 ShockBurst, 32-byte payload)");
}

void Nrf905Pluggit::dump_config() {
  ESP_LOGCONFIG(TAG, "nRF905 Pluggit (868.4 MHz):");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  CE Pin: ", this->ce_pin_);
  LOG_PIN("  TXE Pin: ", this->txe_pin_);
  LOG_PIN("  PWR Pin: ", this->pwr_pin_);
  LOG_PIN("  DR Pin: ", this->dr_pin_);
}

void Nrf905Pluggit::loop() {
  if (this->dr_pin_ == nullptr || !this->dr_pin_->digital_read()) {
    return;
  }
  this->read_payload_();
}

void Nrf905Pluggit::write_config_() {
  // CH_NO = 0x76, HFREQ_PLL=1 → 422.4 + 0x76 * 0.2 = 868.4 MHz
  const uint8_t cfg[10] = {
      0x76,  // CH_NO
      0x0C,  // PA_PWR=10 dBm, HFREQ_PLL=1
      0x44,  // TX_AFW=4, RX_AFW=4
      0x20,  // RX_PW = 32
      0x20,  // TX_PW = 32
      0xE7,  0xE7, 0xE7, 0xE7,  // RX address (learn / broadcast)
      0xD8,  // CRC 16-bit, CRC_EN, XOF=16 MHz
  };
  this->ce_pin_->digital_write(false);
  this->enable();
  this->write_byte(NRF_CMD_WC);
  this->write_array(cfg, sizeof(cfg));
  this->disable();
}

void Nrf905Pluggit::enter_rx_() {
  this->txe_pin_->digital_write(false);
  this->ce_pin_->digital_write(true);
}

void Nrf905Pluggit::read_payload_() {
  uint8_t buf[NRF_PAYLOAD_LEN];
  this->ce_pin_->digital_write(false);
  this->enable();
  this->write_byte(NRF_CMD_RRP);
  this->read_array(buf, NRF_PAYLOAD_LEN);
  this->disable();
  this->enter_rx_();

  const std::string hex = this->to_hex_(buf, NRF_PAYLOAD_LEN);
  ESP_LOGI(TAG, "RX %s", hex.c_str());

  if (this->last_packet_ != nullptr) {
    this->last_packet_->publish_state(hex);
  }
  if (this->rf_rx_ != nullptr) {
    this->rf_rx_->publish_state(true);
  }
  for (auto *trigger : this->on_packet_) {
    trigger->trigger(hex);
  }
}

void Nrf905Pluggit::transmit_hex(const std::string &hex) {
  uint8_t buf[NRF_PAYLOAD_LEN] = {0};
  this->parse_hex_(hex, buf, NRF_PAYLOAD_LEN);

  this->ce_pin_->digital_write(false);
  this->txe_pin_->digital_write(true);
  this->enable();
  this->write_byte(NRF_CMD_WTP);
  this->write_array(buf, NRF_PAYLOAD_LEN);
  this->disable();
  this->ce_pin_->digital_write(true);
  delay(20);
  this->enter_rx_();
  ESP_LOGI(TAG, "TX %s", this->to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
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
