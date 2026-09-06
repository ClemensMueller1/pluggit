# Verdrahtung Pluggit Avent P (P300 / P450)

Arbeiten an der KWL nur bei **gezogenem Netzstecker**. Die Steuereingänge
J8 führen kein 230 V, die Anlage selbst schon.

## Klemmleiste J8 (externe Stufenschaltung)

Laut Betriebsanleitung Avent P300 / P300N / P450 und KNX-User-Forum:

| Klemme | Aderfarbe APKB1 | Funktion |
|---|---|---|
| J8-2 | braun | Kontakt Stufe 1 |
| J8-3 | grün | Gemeinsamer Pol der Kontakte |
| J8-4 | gelb | Kontakt Stufe 3 |
| J8-7 | blau | Analog + (3 / 6 / 9 V) |
| J8-8 | rot | Analog − |

Logik der **Kontakte** (potentialfrei, Schließer):

```
Stufe 1  J8-3 ──●── J8-2
Stufe 2  (kein Kontakt geschlossen)
Stufe 3  J8-3 ──●── J8-4
UNGÜLTIG beide Kontakte gleichzeitig  → nicht ansteuern
```

Logik **Analog** (potentialfrei):

```
J8-7 (+) ── 3 V → Stufe 1
           6 V → Stufe 2
           9 V → Stufe 3
           0 V → Fernbedienung / Wochenprogramm
J8-8 (−)
```

Kontakt- und Analogbetrieb nicht gleichzeitig nutzen.

## Shelly 1 Mini Gen3 (Weg A)

Jeder Mini braucht 230 V auf L/N (oder 24 V DC laut Datenblatt).
Die Lastklemmen **O / I** sind der potentialfreie Schließer.

```
Shelly „Stufe 1“
  O ────────────── J8-3 (grün)
  I ────────────── J8-2 (braun)

Shelly „Stufe 3“
  O ────────────── J8-3 (grün)   (gleiche Klemme, parallel)
  I ────────────── J8-4 (gelb)

230 V L/N ────── beide Shellys (eigene Sicherung)
```

In Home Assistant beide Geräte als Switch einbinden, dann Integration
„Pluggit Avent P“ → Relais, die beiden Switches auswählen.

Die Integration schaltet **immer zuerst beide aus** (250 ms), danach
höchstens einen Kontakt zu.

## ESP32 + Relaismodul (Weg B)

```
ESP32 5 V / VIN ──── Relais VCC
ESP32 GND       ──── Relais GND
ESP32 GPIO18    ──── Relais IN1  (Stufe 1)
ESP32 GPIO19    ──── Relais IN2  (Stufe 3)

Relais1 COM ──── J8-3
Relais1 NO  ──── J8-2
Relais2 COM ──── J8-3
Relais2 NO  ──── J8-4
```

JD-VCC des Relaismoduls nach Möglichkeit mit 5 V, Optokoppler-Seite mit
3,3 V versorgen (Jumper JD-VCC auf manchen Boards abziehen).

## Analog 0–10 V (Weg C)

```
12-V-Netzteil + ──── Wandler VCC
12-V-Netzteil − ──── Wandler GND
ESP32 GPIO26     ──── Wandler PWM DIN
ESP32 GND        ──── Wandler PWM GND

Wandler AO   ──── J8-7 blau
Wandler AGND ──── J8-8 rot
```

Kein Schutzleiter, kein 230-V-N auf J8-8.

Nach dem Flashen mit Multimeter prüfen: 30 % PWM ≈ 3 V, 60 % ≈ 6 V,
90 % ≈ 9 V. Trimmpoti auf dem Wandler bei Bedarf nachstimmen.

## Störmeldekontakt J3 (optional)

Potentialfreier Ausgang, max. 230 V / 5 A laut Handbuch:

- J3-3 / J3-6 = Sammelstörung

An den Eingang eines dritten Shelly oder an einen ESP32-GPIO mit
Optokoppler legen → Binary-Sensor „Störung“. Im MQTT-Gateway als
`pluggit_avent/fault` veröffentlichen.

## UART-Weiche (Weg D)

Funkmodul-Stecker auf der Hauptplatine: 4–6 Pins, 3,3-V-UART, 4800 8N1.
RX/TX/GND des ESP32 dazwischen. **Kein 5-V-Arduino.**
USB-Versorgung des ESP32 muss dauerhaft stehen, sonst fällt die
Original-Fernbedienung aus (Originalprojekt d00616/P300).

Ohne gesteckten ESP32 muss die grüne LED des Funkmoduls nach dem
Einschalten leuchten. Mit ESP32 kommt innerhalb einer Sekunde eine
zweite LED dazu.

## Funk (Weg E)

nRF905 nur mit 3,3 V. Antenne aufschrauben. ESP32 und Modul in
Gehäuse in Sichtweite der KWL (nicht im Metallschrank).
Sniffer-Log: Taste an der Original-FB drücken, 32-Byte-Hex notieren.
Replay über MQTT-Topic `pluggit_avent/rf/tx`.
