#include "nrf905_pluggit.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
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
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->sniff_mode_ ? "sniff (CRC off, addr[0]=0x00)" : "replay CRC-16");
  ESP_LOGCONFIG(TAG, "  NVM address: %s",
                this->nvm_valid_ ? to_hex_(this->nvm_address_, 4).c_str() : "unset");
  ESP_LOGCONFIG(TAG, "  Address: %s",
                this->has_address_() ? to_hex_(this->rx_address_, 4).c_str() : "unset");
  ESP_LOGCONFIG(TAG, "  Link test: addr 54 45 53 54 CRC-16 cfg 76 0E 44 20 20 54 45 53 54 D8");
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
    // Match one address byte, CRC off, so the other three address bytes land
    // in the payload. Link-test frames start with 0x54, not the Pluggit 0x00,
    // and a CRC-16 transmitter is still delivered when the receiver's CRC is off.
    cfg[2] = 0x41;  // TX_AFW=4, RX_AFW=1
    cfg[5] = this->sniff_prefix_;
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
static const uint8_t TEST_CFG[10] = {0x76, 0x0E, 0x44, 0x20, 0x20, 0x54, 0x45, 0x53, 0x54, 0xD8};

bool Nrf905Pluggit::program_test_radio_() {
  if (this->is_failed()) {
    ESP_LOGW(TAG, "TEST skipped: nRF905 setup failed");
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
    ESP_LOGE(TAG, "TEST config readback mismatch — check SPI, both modules must show cfg 76 0E 44 20 20 54 45 53 54 D8");
    return false;
  }
  this->write_tx_address_bytes_(TEST_CFG + 5);
  this->test_mode_ = true;
  ESP_LOGI(TAG, "TEST radio cfg %s (868.4 MHz, addr 54 45 53 54, AFW 4/4, PW 32, CRC-16)",
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
  this->save_address_();
  this->write_config_();
  this->write_tx_address_();
  this->enter_rx_();
  const std::string addr = to_hex_(this->rx_address_, 4);
  ESP_LOGI(TAG, "Locked ShockBurst address %s (CRC-16). Press each remote key (Aus/1/2/3) and store the packets in HA",
           addr.c_str());
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

  if (this->test_mode_) {
    // CRC-16 is on, so DR only fires when the frame matched address and CRC.
    ESP_LOGI(TAG, "TEST RX CRC-16 OK %s", to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
    this->publish_packet_(buf, NRF_PAYLOAD_LEN);
    return;
  }

  if (this->sniff_mode_) {
    // tx/test address is 54 45 53 54. With a 1-byte match on 0x54 the RX
    // buffer starts with 45 53 54 and then the 32-byte payload.
    const bool link_test = this->sniff_prefix_ == 0x54 && buf[0] == 0x45 && buf[1] == 0x53 && buf[2] == 0x54;
    if (link_test) {
      char text[30];
      size_t n = 0;
      for (size_t i = 3; i < NRF_PAYLOAD_LEN && n + 1 < sizeof(text); i++) {
        const uint8_t c = buf[i];
        if (c == 0)
          break;
        text[n++] = (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
      }
      text[n] = '\0';
      ESP_LOGI(TAG, "SNIFF ShockBurst address 54 45 53 54 message \"%s\"", text);
      if (this->listen_address_ != nullptr)
        this->listen_address_->publish_state("54 45 53 54");
      this->publish_packet_(buf, NRF_PAYLOAD_LEN);
      return;
    }
    const uint32_t now = millis();
    if (now - this->last_noise_ms_ > 1000) {
      this->last_noise_ms_ = now;
      ESP_LOGD(TAG, "RX noise %s", to_hex_(buf, NRF_PAYLOAD_LEN).c_str());
    }
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
  this->test_mode_ = false;
  this->sniff_mode_ = true;
  // Stay on the link-test prefix. Matching 0x00 here only hears unrelated
  // traffic and used to lock a random address before tx/test could arrive.
  this->sniff_prefix_ = 0x54;
  this->write_config_();
  this->enter_rx_();
  ESP_LOGI(TAG, "Sniff CRC off, match byte 0x54. tx/test shows address 54 45 53 54. Noise is not locked.");
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state("sniff 54");
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
  memcpy(this->rx_address_, buf, 4);
  this->test_mode_ = false;
  this->sniff_mode_ = false;
  this->save_address_();
  this->write_config_();
  this->write_tx_address_();
  this->enter_rx_();
  const std::string pretty = to_hex_(this->rx_address_, 4);
  ESP_LOGI(TAG, "ShockBurst address set to %s and saved to NVM", pretty.c_str());
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state(pretty);
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
    this->leave_test_();
    return;
  }
  if (!this->program_test_radio_())
    return;
  this->enter_rx_();
  ESP_LOGI(TAG, "TEST listen — publish tx/test on the other module (addr 54 45 53 54, CRC-16)");
  if (this->listen_address_ != nullptr)
    this->listen_address_->publish_state("54 45 53 54");
  if (this->rf_rx_ != nullptr)
    this->rf_rx_->publish_state(false);
}

void Nrf905Pluggit::transmit_test(const std::string &message) {
  uint8_t buf[NRF_PAYLOAD_LEN] = {0};
  fill_test_payload_(message, buf);
  if (!this->program_test_radio_())
    return;

  // A few copies so a sniffer that has just switched config still catches one.
  for (int i = 0; i < 4; i++) {
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
    if (i + 1 < 4)
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
    ESP_LOGD(TAG, "TEST listen 868.4 MHz addr 54 45 53 54 CRC-16 DR=%d CD=%d AM=%d", dr, cd, am);
  } else if (this->sniff_mode_) {
    ESP_LOGD(TAG, "sniff 868.4 MHz prefix %02X CRC off DR=%d CD=%d AM=%d — waiting for tx/test",
             this->sniff_prefix_, dr, cd, am);
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
