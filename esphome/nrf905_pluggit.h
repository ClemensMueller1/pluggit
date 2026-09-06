#pragma once
// Minimal nRF905 receiver for the Pluggit DTH-029255-04 remote
// (868.4 MHz, 32-byte ShockBurst payload).
//
// Register programming follows the nRF905 product spec and the
// KNX-User-Forum capture (868.4 MHz, GFSK).

#include "esphome.h"
#include <SPI.h>

static const uint8_t NRF_WC = 0x00;
static const uint8_t NRF_RC = 0x10;
static const uint8_t NRF_WTP = 0x20;
static const uint8_t NRF_RRP = 0x24;

class PluggitNrf905 : public Component {
 public:
  PluggitNrf905(uint8_t cs, uint8_t ce, uint8_t txe, uint8_t pwr, uint8_t dr)
      : cs_(cs), ce_(ce), txe_(txe), pwr_(pwr), dr_(dr) {}

  void setup() override {
    pinMode(cs_, OUTPUT);
    pinMode(ce_, OUTPUT);
    pinMode(txe_, OUTPUT);
    pinMode(pwr_, OUTPUT);
    pinMode(dr_, INPUT);
    digitalWrite(cs_, HIGH);
    digitalWrite(ce_, LOW);
    digitalWrite(txe_, LOW);
    digitalWrite(pwr_, HIGH);
    delay(5);
    SPI.begin();
    write_config();
    // Receive mode
    digitalWrite(txe_, LOW);
    digitalWrite(ce_, HIGH);
    ESP_LOGI("nrf905", "listening on 868.4 MHz");
  }

  void loop() override {
    if (digitalRead(dr_) != HIGH) {
      return;
    }
    uint8_t buf[32];
    digitalWrite(ce_, LOW);
    digitalWrite(cs_, LOW);
    SPI.transfer(NRF_RRP);
    for (int i = 0; i < 32; i++) {
      buf[i] = SPI.transfer(0x00);
    }
    digitalWrite(cs_, HIGH);
    digitalWrite(ce_, HIGH);

    char hex[97];
    for (int i = 0; i < 32; i++) {
      sprintf(hex + i * 3, "%02X ", buf[i]);
    }
    hex[95] = 0;
    ESP_LOGI("nrf905", "RX %s", hex);
    if (id(last_packet) != nullptr) {
      id(last_packet).publish_state(hex);
    }
    if (id(rf_rx) != nullptr) {
      id(rf_rx).publish_state(true);
    }
  }

 private:
  uint8_t cs_, ce_, txe_, pwr_, dr_;

  void write_config() {
    // CH_NO = 0x76 → 868.4 MHz (HFREQ_PLL=1, 422.4 + 0x76*0.2 = 868.4)
    uint8_t cfg[10] = {
        0x76,        // CH_NO
        0x0C,        // AUTO_RETRAN=0, RX_RED_PWR=0, PA_PWR=10dBm, HFREQ_PLL=1
        0x44,        // TX_AFW=4, RX_AFW=4
        0x20,        // RX_PW = 32
        0x20,        // TX_PW = 32
        0xE7, 0xE7,  // RX address bytes 0-1 (broadcast / learn)
        0xE7, 0xE7,  // RX address bytes 2-3
        0xD8,        // CRC_MODE=16bit, CRC_EN=1, XOF=16MHz, UP_CLK_EN=0
    };
    digitalWrite(cs_, LOW);
    SPI.transfer(NRF_WC);
    for (uint8_t b : cfg) {
      SPI.transfer(b);
    }
    digitalWrite(cs_, HIGH);
  }
};
