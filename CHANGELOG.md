# Changelog

## 1.1.2

Release [v1.1.2](https://github.com/ClemensMueller1/pluggit/releases/tag/v1.1.2).

### Home Assistant

- MQTT-Setup wartet, bis der Broker-Client bereit ist.
- Vier 32-Byte-Funkpakete in der MQTT-Konfiguration: Aus, Stufe 1, Stufe 2, Stufe 3. Die Fan-Entität sendet sie auf `rf/tx`.
- ShockBurst-Adresse in der MQTT-Konfiguration anzeigen oder setzen. Der ESP speichert einen gesetzten Wert im NVM und meldet die gelernte Adresse zurück.
- Geräteinfo verweist auf diese Datei und auf das Release-Tag.

### nRF905-Firmware

- Externes ESPHome-Component, 868,4 MHz, CRC-16. CSN an GPIO5, CE an GPIO21.
- Beim Start Replay mit der Adresse aus dem NVM. Das Log zeigt `NVM ShockBurst address`.
- Sniff nur über **RF Adresse neu lernen**.
- Link-Test zweier Module: `tx/test` sendet, `rx/test` empfängt. Feste Adresse `54 45 53 54`, CRC-16. Sniff auf diesem Präfix loggt die Adresse und die Nachricht, ohne Rauschen als Adresse zu speichern.
- Die YAML lädt die Komponente von `github://ClemensMueller1/pluggit@v1.1.2`.
