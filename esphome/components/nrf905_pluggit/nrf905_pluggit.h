#pragma once

#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace nrf905_pluggit {

static const uint8_t NRF_CMD_WC = 0x00;
static const uint8_t NRF_CMD_RC = 0x10;
static const uint8_t NRF_CMD_WTP = 0x20;
static const uint8_t NRF_CMD_WTA = 0x22;
static const uint8_t NRF_CMD_RRP = 0x24;
static const uint8_t NRF_STATUS_DR = 0x20;
static const uint8_t NRF_STATUS_AM = 0x80;
static const size_t NRF_PAYLOAD_LEN = 32;

class Nrf905Pluggit : public Component,
                      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                            spi::DATA_RATE_1MHZ> {
 public:
  void set_ce_pin(GPIOPin *pin) { this->ce_pin_ = pin; }
  void set_txe_pin(GPIOPin *pin) { this->txe_pin_ = pin; }
  void set_pwr_pin(GPIOPin *pin) { this->pwr_pin_ = pin; }
  void set_dr_pin(GPIOPin *pin) { this->dr_pin_ = pin; }
  void set_cd_pin(GPIOPin *pin) { this->cd_pin_ = pin; }
  void set_am_pin(GPIOPin *pin) { this->am_pin_ = pin; }
  void set_last_packet(text_sensor::TextSensor *sensor) { this->last_packet_ = sensor; }
  void set_listen_address(text_sensor::TextSensor *sensor) { this->listen_address_ = sensor; }
  void set_rf_rx(binary_sensor::BinarySensor *sensor) { this->rf_rx_ = sensor; }
  void set_radio_ok(binary_sensor::BinarySensor *sensor) { this->radio_ok_ = sensor; }
  void set_rx_address(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    this->rx_address_[0] = b0;
    this->rx_address_[1] = b1;
    this->rx_address_[2] = b2;
    this->rx_address_[3] = b3;
    this->address_from_yaml_ = true;
    this->sniff_mode_ = false;
  }
  void register_on_packet_trigger(Trigger<std::string> *trigger) { this->on_packet_.push_back(trigger); }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void transmit_hex(const std::string &hex);
  void start_sniff();
  void set_address_hex(const std::string &hex);

 protected:
  void enter_standby_();
  void write_config_();
  void write_tx_address_();
  bool verify_config_();
  void enter_rx_();
  void read_payload_();
  bool looks_like_pluggit_(const uint8_t *buf) const;
  bool has_address_() const;
  void lock_address_(const uint8_t *sniff_buf);
  void save_address_();
  void load_address_();
  void publish_packet_(const uint8_t *buf, size_t len);
  void log_heartbeat_();
  static std::string to_hex_(const uint8_t *data, size_t len);
  static size_t parse_hex_(const std::string &hex, uint8_t *out, size_t max_len);

  GPIOPin *ce_pin_{nullptr};
  GPIOPin *txe_pin_{nullptr};
  GPIOPin *pwr_pin_{nullptr};
  GPIOPin *dr_pin_{nullptr};
  GPIOPin *cd_pin_{nullptr};
  GPIOPin *am_pin_{nullptr};
  text_sensor::TextSensor *last_packet_{nullptr};
  text_sensor::TextSensor *listen_address_{nullptr};
  binary_sensor::BinarySensor *rf_rx_{nullptr};
  binary_sensor::BinarySensor *radio_ok_{nullptr};
  std::vector<Trigger<std::string> *> on_packet_;

  bool sniff_mode_{false};
  bool address_from_yaml_{false};
  bool nvm_valid_{false};
  uint8_t rx_address_[4]{0x00, 0x00, 0x00, 0x00};
  uint8_t nvm_address_[4]{0x00, 0x00, 0x00, 0x00};
  uint8_t last_cfg_[10]{};
  uint32_t last_noise_ms_{0};
  ESPPreferenceObject pref_{};
};

class Nrf905PacketTrigger : public Trigger<std::string> {};

template<typename... Ts> class Nrf905TransmitAction : public Action<Ts...> {
 public:
  explicit Nrf905TransmitAction(Nrf905Pluggit *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, data)
  void play(const Ts &...x) override { this->parent_->transmit_hex(this->data_.value(x...)); }

 protected:
  Nrf905Pluggit *parent_;
};

template<typename... Ts> class Nrf905StartSniffAction : public Action<Ts...> {
 public:
  explicit Nrf905StartSniffAction(Nrf905Pluggit *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->start_sniff(); }

 protected:
  Nrf905Pluggit *parent_;
};

template<typename... Ts> class Nrf905SetAddressAction : public Action<Ts...> {
 public:
  explicit Nrf905SetAddressAction(Nrf905Pluggit *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, data)
  void play(const Ts &...x) override { this->parent_->set_address_hex(this->data_.value(x...)); }

 protected:
  Nrf905Pluggit *parent_;
};

}  // namespace nrf905_pluggit
}  // namespace esphome
