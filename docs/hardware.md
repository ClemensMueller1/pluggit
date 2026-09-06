# Hardware und Bezugsquellen

Stand: September 2026. Preise sind Richtwerte inkl. MwSt., Versand extra.

Die Anlage auf den Fotos ist eine **Pluggit Avent P** (P300 / P300N / P450,
Baujahr um 2011) mit Funk-Fernbedienung **DTH-029255-04**
(PN `4DTH0011P01V04E06`, Date 1148). Diese Generation hat **kein LAN und
kein Modbus-TCP**. Die späteren AP190 / AP310 / AP460 sind eine andere
Baureihe — dafür existieren fertige HACS-Integrationen
([Tvalley71/pluggit](https://github.com/Tvalley71/pluggit)).

---

## Empfehlung nach Aufwand

| Weg | Was du bekommst | Aufwand | Kosten | Öffnen der KWL? |
|---|---|---|---|---|
| **A — 2× Shelly 1 Mini** | Stufe 1 / 2 / 3 | gering | ~22 € | nur Klemmleiste J8 |
| **B — ESP32 + Relais** | Stufe 1 / 2 / 3, MQTT | gering | ~15 € | nur J8 |
| **C — ESP32 + 0–10 V** | Stufe 1 / 2 / 3 + „Fernbedienung“ | mittel | ~25 € | nur J8 |
| **D — UART-Weiche** | Stufen + Temperaturen | hoch | ~12 € | ja, 3,3 V only |
| **E — nRF905 Funk** | Fernbedienung ersetzen | hoch, experimentell | ~20 € | nein |

**Start mit A oder B.** C ist die sauberste offizielle Schnittstelle.
D und E nur, wenn du Temperaturen oder die Original-Fernbedienung
nachbauen willst.

---

## Stückliste A — Shelly (ohne Löten)

Zwei **potentialfreie** Relais. Nicht den Shelly Plus 2PM nehmen — dessen
Kontakte sind **nicht** potentialfrei.

| Stück | Artikel | Bezugsquelle | ca. |
|---|---|---|---|
| 2 | **Shelly 1 Mini Gen3** (1 Kanal, 8 A, potentialfreier Kontakt) | [Amazon.de](https://www.amazon.de/Shelly-Mini-Gen3-Potentialfreier-Garagentor%C3%B6ffner/dp/B0CQCHS2QS) · [idealo](https://www.idealo.de/preisvergleich/OffersOfProduct/205454481_-1-mini-gen3-shelly.html) · [BerryBase](https://www.berrybase.de/) | 10–12 € / Stk. |
| alternativ 2 | **Shelly 1 Gen3** (16 A, etwas größer, ebenfalls potentialfrei) | [Amazon.de](https://www.amazon.de/Shelly-1-Gen3-Familie/dp/B0DHP68FF5) | ~13 € / Stk. |
| 1 | 5-V-USB-Netzteil oder 230-V-Versorgung der Shellys | Haushalt | — |
| etwas | Litze 0,25–0,5 mm², Adernendhülsen | Conrad / Reichelt / Hornbach | 5 € |

Verdrahtung: siehe [verdrahtung.md](verdrahtung.md).

Home Assistant: Shelly-Integration (lokal, kein Cloud-Zwang) → danach
diese Custom-Integration im Relais-Modus auf die beiden Schalter legen.

---

## Stückliste B — ESP32 + 2-Kanal-Relais

| Stück | Artikel | Bezugsquelle | ca. |
|---|---|---|---|
| 1 | ESP32-DevKitC / NodeMCU-32S | [Amazon](https://www.amazon.de/s?k=ESP32+DevKit) · [BerryBase ESP32](https://www.berrybase.de/) · [Reichelt](https://www.reichelt.de/) | 6–10 € |
| 1 | 2-Kanal-Relais 5 V, Optokoppler, NO | Amazon „2 Kanal Relais 5V Optokoppler“ · BerryBase | 4–7 € |
| 1 | USB-Netzteil 5 V / 1 A | Haushalt | — |
| 1 | Jumperkabel Dupont | Amazon / BerryBase | 3 € |
| optional | Hutschienen-Gehäuse 3 TE | Reichelt, Hager | 8 € |

Firmware: `esphome/pluggit-avent-relais.yaml`.

Relais **COM** jeweils an J8-3 (grün), **NO** Relais 1 an J8-2 (braun),
**NO** Relais 2 an J8-4 (gelb). Relais müssen potentialfrei schalten
(kein 230 V auf den Kontakten).

---

## Stückliste C — analog 0–10 V

Offizielle Schnittstelle laut Handbuch: 3 V / 6 V / 9 V potentialfrei auf
J8-7 (+) blau und J8-8 (−) rot.

| Stück | Artikel | Bezugsquelle | ca. |
|---|---|---|---|
| 1 | ESP32-DevKit | s. oben | 8 € |
| 1 | **PWM 3,3 V → 0–10 V DAC-Modul** | [Amazon Fafeicy 3,3V PWM zu 0-10V](https://www.amazon.de/Fafeicy-0-10V-Konverter-industrielle-Steuerung/dp/B08C7H8SZJ) | 8–12 € |
| alternativ | Wemos-D1-Mini-Shield 2× 0–10 V (GarloTEC) | [Amazon](https://www.amazon.de/GarloTEC-Shield-Digital-Analog-Converter/dp/B0DWJVKH8S) | ~25 € |
| 1 | Steckernetzteil 12 V / 0,5 A (für den Wandler) | Amazon / Conrad | 8 € |

Firmware: `esphome/pluggit-avent-analog.yaml`.

AO des Wandlers → J8-7, AGND → J8-8. Masse des Wandlers **nicht** mit
Schutzleiter der KWL verbinden.

---

## Stückliste D — UART-Weiche im Gerät

Nur für Fortgeschrittene. 5-V-Logik zerstört die Platine.

| Stück | Artikel | Bezugsquelle | ca. |
|---|---|---|---|
| 1 | ESP32-DevKit (3,3 V) | s. oben | 8 € |
| 1 | Micro-USB-Kabel, flacher Stecker | Amazon | 4 € |
| 1 | USB-Netzteil 5 V | Haushalt | — |
| optional | Teensy 3.2 (Originalprojekt) | [PJRC](https://www.pjrc.com/store/teensy32.html) | ~25 € |

Firmware: `esphome/pluggit-avent-uart.yaml`.
Referenz: [d00616/P300](https://github.com/d00616/P300)
(Softwarestand Funkmodul **03.08.01**, nicht 02.00.03).

---

## Stückliste E — Funk-Fernbedienung ersetzen (nRF905)

Die Original-FB sendet auf **868,4 MHz** mit Nordic **nRF905**
([KNX-User-Forum](https://knx-user-forum.de/forum/%C3%B6ffentlicher-bereich/knx-eib-forum/25323-pluggit-l%C3%BCftungsanlage-anbinden/page15)).

| Stück | Artikel | Bezugsquelle | ca. |
|---|---|---|---|
| 1 | ESP32-DevKit | s. oben | 8 € |
| 1 | **nRF905-Modul 868 MHz** mit SMA-Antenne (PTR8000+ / NF905SE) | [Amazon Hailege NRF905](https://www.amazon.de/NRF905-Funksender-PTR8000-Antenne-NF905SE/dp/B07XYYQLKD) · [Botland](https://botland.de/funkmodule/2747-nrf905-433868915mhz-funkmodul-tht-transceiver-mit-antenne-5903351242608.html) | 7–10 € |

Firmware: `esphome/pluggit-avent-rf.yaml` plus External Component
`esphome/components/nrf905_pluggit/` (ESPHome 2026.3+, kein custom_component).
Arduino-Variante: `firmware/nrf905_sniffer.ino`.

Zuerst sniffen, Geräteadresse der eigenen Fernbedienung notieren, dann
gezielt Replay. Das Pairing der Original-FB bleibt erhalten, solange du
dieselbe Adresse verwendest.

---

## Was **nicht** passt

- HACS-Integration **Tvalley71/pluggit** bzw. **Goemon64/Pluggit-HA** —
  nur AP190 / AP310 / AP460 mit Modbus-TCP.
- IR-Blaster / Broadlink — die Fernbedienung ist **Funk**, kein Infrarot.
- 433-MHz-CC1101-Module — falsches Band (868,4 MHz, nRF905).
- Shelly Plus 2PM — Relais **nicht** potentialfrei.

---

## Ersatz-Originalteile

Die Original-Fernbedienung und das Zubehörkabel **APKB1** sind kaum noch
im freien Handel. Anfragen bei Pluggit-Fachbetrieben / Dantherm Group
(Pluggit gehört zur Dantherm Group). Für die Steuerung braucht man sie
nicht, wenn J8 verdrahtet wird.
