#pragma once

#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace nrf905_pluggit {

static const uint8_t NRF_CMD_WC = 0x00;
static const uint8_t NRF_CMD_WTP = 0x20;
static const uint8_t NRF_CMD_RRP = 0x24;
static const size_t NRF_PAYLOAD_LEN = 32;

class Nrf905Pluggit : public Component,
                      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                            spi::DATA_RATE_1MHZ> {
 public:
  void set_ce_pin(GPIOPin *pin) { this->ce_pin_ = pin; }
  void set_txe_pin(GPIOPin *pin) { this->txe_pin_ = pin; }
  void set_pwr_pin(GPIOPin *pin) { this->pwr_pin_ = pin; }
  void set_dr_pin(GPIOPin *pin) { this->dr_pin_ = pin; }
  void set_last_packet(text_sensor::TextSensor *sensor) { this->last_packet_ = sensor; }
  void set_rf_rx(binary_sensor::BinarySensor *sensor) { this->rf_rx_ = sensor; }
  void register_on_packet_trigger(Trigger<std::string> *trigger) { this->on_packet_.push_back(trigger); }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void transmit_hex(const std::string &hex);

 protected:
  void write_config_();
  void enter_rx_();
  void read_payload_();
  static std::string to_hex_(const uint8_t *data, size_t len);
  static size_t parse_hex_(const std::string &hex, uint8_t *out, size_t max_len);

  GPIOPin *ce_pin_{nullptr};
  GPIOPin *txe_pin_{nullptr};
  GPIOPin *pwr_pin_{nullptr};
  GPIOPin *dr_pin_{nullptr};
  text_sensor::TextSensor *last_packet_{nullptr};
  binary_sensor::BinarySensor *rf_rx_{nullptr};
  std::vector<Trigger<std::string> *> on_packet_;
};

class Nrf905PacketTrigger : public Trigger<std::string> {};

template<typename... Ts> class Nrf905TransmitAction : public Action<Ts...> {
 public:
  explicit Nrf905TransmitAction(Nrf905Pluggit *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, data)
  void play(Ts... x) override { this->parent_->transmit_hex(this->data_.value(x...)); }

 protected:
  Nrf905Pluggit *parent_;
};

}  // namespace nrf905_pluggit
}  // namespace esphome
