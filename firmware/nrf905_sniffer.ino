/*
 * Pluggit Avent P — nRF905 868,4-MHz-Sniffer / Replay
 *
 * Hardware: ESP32-DevKit + nRF905 868-MHz-Modul (3,3 V)
 * MQTT: veröffentlicht jedes empfangene 32-Byte-Paket und
 * akzeptiert Replay-Payloads auf pluggit_avent/rf/tx
 *
 * SPI: SCK=18 MOSI=23 MISO=19 CSN=5 CE=4 TXE=16 PWR=17 DR=15
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>

const char *WIFI_SSID = "DeinWLAN";
const char *WIFI_PASS = "wlan-passwort";
const char *MQTT_HOST = "192.168.1.10";
const char *MQTT_USER = "";
const char *MQTT_PASS = "";
const char *MQTT_RX = "pluggit_avent/rf/rx";
const char *MQTT_TX = "pluggit_avent/rf/tx";
const char *MQTT_AVAIL = "pluggit_avent/availability";

const uint8_t PIN_CS = 5;
const uint8_t PIN_CE = 4;
const uint8_t PIN_TXE = 16;
const uint8_t PIN_PWR = 17;
const uint8_t PIN_DR = 15;

WiFiClient wifi;
PubSubClient mqtt(wifi);

static const uint8_t NRF_WC = 0x00;
static const uint8_t NRF_WTP = 0x20;
static const uint8_t NRF_RRP = 0x24;

void nrfSelect() { digitalWrite(PIN_CS, LOW); }
void nrfDeselect() { digitalWrite(PIN_CS, HIGH); }

void nrfConfig() {
  uint8_t cfg[] = {0x76, 0x0C, 0x44, 0x20, 0x20, 0xE7, 0xE7, 0xE7, 0xE7, 0xD8};
  nrfSelect();
  SPI.transfer(NRF_WC);
  for (uint8_t b : cfg) SPI.transfer(b);
  nrfDeselect();
}

void nrfRxMode() {
  digitalWrite(PIN_TXE, LOW);
  digitalWrite(PIN_CE, HIGH);
}

void nrfTx(const uint8_t *payload, size_t len) {
  digitalWrite(PIN_CE, LOW);
  digitalWrite(PIN_TXE, HIGH);
  nrfSelect();
  SPI.transfer(NRF_WTP);
  for (size_t i = 0; i < 32; i++) {
    SPI.transfer(i < len ? payload[i] : 0x00);
  }
  nrfDeselect();
  digitalWrite(PIN_CE, HIGH);
  delay(20);
  nrfRxMode();
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

void onMqtt(char *topic, byte *payload, unsigned int length) {
  if (strcmp(topic, MQTT_TX) != 0) return;
  uint8_t buf[32] = {0};
  size_t n = 0;
  for (unsigned int i = 0; i + 1 < length && n < 32; i++) {
    int hi = hexNibble((char)payload[i]);
    int lo = hexNibble((char)payload[i + 1]);
    if (hi < 0 || lo < 0) continue;
    buf[n++] = (uint8_t)((hi << 4) | lo);
    i++;
  }
  if (n > 0) {
    nrfTx(buf, n);
    Serial.printf("TX %u bytes\n", (unsigned)n);
  }
}

void mqttConnect() {
  while (!mqtt.connected()) {
    if (mqtt.connect("pluggit-nrf905", MQTT_USER, MQTT_PASS, MQTT_AVAIL, 1, true, "offline")) {
      mqtt.publish(MQTT_AVAIL, "online", true);
      mqtt.subscribe(MQTT_TX);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_CE, OUTPUT);
  pinMode(PIN_TXE, OUTPUT);
  pinMode(PIN_PWR, OUTPUT);
  pinMode(PIN_DR, INPUT);
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_CE, LOW);
  digitalWrite(PIN_TXE, LOW);
  digitalWrite(PIN_PWR, HIGH);
  delay(5);
  SPI.begin();
  nrfConfig();
  nrfRxMode();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  mqtt.setServer(MQTT_HOST, 1883);
  mqtt.setCallback(onMqtt);
}

void loop() {
  if (!mqtt.connected()) mqttConnect();
  mqtt.loop();
  if (digitalRead(PIN_DR) == HIGH) {
    uint8_t buf[32];
    digitalWrite(PIN_CE, LOW);
    nrfSelect();
    SPI.transfer(NRF_RRP);
    for (int i = 0; i < 32; i++) buf[i] = SPI.transfer(0);
    nrfDeselect();
    nrfRxMode();
    char hex[97];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 3, "%02X ", buf[i]);
    hex[95] = 0;
    Serial.println(hex);
    mqtt.publish(MQTT_RX, hex, false);
  }
}
