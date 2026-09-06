# ACHTUNG: VOLLKOMMEN UNGETESTET...Pluggit Avent P für Home Assistant

Steuert die **Pluggit Avent P** (P300 / P300N / P450) mit der grauen
Funk-Fernbedienung **DTH-029255-04**. Die Anlage hat kein LAN — die
späteren AP190/AP310-Integrationen funktionieren hier nicht.

```
Gerät     Pluggit Avent P (Front „Avent P“, lila Wolkenhimmel)
Fernbedienung  DTH-029255-04  PN 4DTH0011P01V04E06  Date 1148
```

Drei Wege, eine Home-Assistant-Integration:

1. **Relais an J8** — zwei potentialfreie Schließer, Stufe 1 / 2 / 3
2. **Analog 0–10 V an J8** — 3 / 6 / 9 V, plus „zurück zur Fernbedienung“
3. **MQTT-Gateway** — ESPHome UART im Gerät oder nRF905-Funk (868,4 MHz)

## Schnellstart (empfohlen: 2× Shelly)

Stückliste und Shops: **[docs/hardware.md](docs/hardware.md)**  
Klemmplan: **[docs/verdrahtung.md](docs/verdrahtung.md)**

1. Zwei **Shelly 1 Mini Gen3** (potentialfrei, nicht Plus 2PM) kaufen.
2. Netzstecker der KWL ziehen. J8 öffnen:
   - Shelly 1: O→J8-3 (grün), I→J8-2 (braun) → Stufe 1
   - Shelly 2: O→J8-3 (grün), I→J8-4 (gelb) → Stufe 3
3. Shellys in Home Assistant einbinden.
4. Dieses Repository nach `config/custom_components/pluggit_avent/` kopieren
   (oder als HACS-Custom-Repository hinzufügen).
5. Home Assistant neu starten.
6. **Einstellungen → Geräte & Dienste → Integration hinzufügen →
   Pluggit Avent P** → Relais, die beiden Switches wählen.

Ergebnis: eine Fan-Entität mit Presets *Stufe 1 / 2 / 3*. Stufe 2 = beide
Relais aus. Die Integration schaltet nie beide Kontakte gleichzeitig.

Blueprints unter `blueprints/` (Feuchte-Boost, Abwesenheit) nach
`config/blueprints/automation/pluggit_avent/` kopieren.

## Installation der Integration

### Manuell

```
config/
  custom_components/
    pluggit_avent/     ← kompletter Ordner aus diesem Repo
```

Neustart, dann Integration hinzufügen.

### HACS

HACS → Integrations → ⋮ → Custom repositories → URL dieses Repos,
Typ *Integration*. Danach „Pluggit Avent P“ herunterladen, HA neu starten.

## Anbindungsarten in der UI

| Modus | Quell-Entitäten | Kann |
|---|---|---|
| Relais | 2× `switch` | Stufe 1–3 |
| Analog | 1× `number` (Volt) | Stufe 1–3, 0 V = Fernbedienung |
| MQTT | Topic-Präfix `pluggit_avent` | Stufen + Sensoren (UART/Funk) |

MQTT-Topics des Gateways:

| Topic | Richtung | Inhalt |
|---|---|---|
| `pluggit_avent/speed/set` | HA → Gerät | `1` `2` `3` (`0` = FB, nur Analog) |
| `pluggit_avent/speed` | Gerät → HA | aktuelle Stufe |
| `pluggit_avent/t1` … `t4` | Gerät → HA | Temperaturen °C |
| `pluggit_avent/humidity` | Gerät → HA | % rF |
| `pluggit_avent/filter_alarm` | Gerät → HA | `true`/`false` |
| `pluggit_avent/fault` | Gerät → HA | `true`/`false` |
| `pluggit_avent/availability` | Gerät → HA | `online`/`offline` |

## ESPHome

Vorlagen in `esphome/`. `secrets.yaml.example` nach `secrets.yaml` kopieren.

| Datei | Hardware |
|---|---|
| `pluggit-avent-relais.yaml` | ESP32 + 2 Relais an J8 |
| `pluggit-avent-analog.yaml` | ESP32 + PWM-0–10-V-Wandler an J8-7/8 |
| `pluggit-avent-uart.yaml` | ESP32 3,3 V UART 4800 Baud an der Hauptplatine |
| `pluggit-avent-rf.yaml` | nRF905-Sniffer (experimentell) |

Funk-Replay ohne ESPHome-Custom-Component:
`firmware/nrf905_sniffer.ino`.

Die UART-Firmware spricht das interne Modbus der P300
(4800 8N1, **nur 3,3 V**, Funkmodul-Software 03.08.01).
Register ggf. anpassen — siehe [docs/protocol.md](docs/protocol.md).

## Ohne Custom Component

Paket `packages/pluggit_avent_shelly.yaml` erzeugt einen Template-Fan
aus zwei Shelly-Switches. Entity-IDs anpassen.

## Sicherheit

- KWL spannungsfrei schalten, bevor J8 angefasst wird.
- J8-Kontakte und Analogeingang sind **potentialfrei** — kein 230 V
  auf diese Klemmen.
- UART im Gerät: **kein 5-V-Arduino**, sonst ist die Hauptplatine hin.
- Relais-Interlock nicht umgehen (beide Kontakte gleichzeitig = ungültig).
- Eingriff kann Garantie und Zulassung der Anlage berühren.
- Keine Gewährleistung, Nachbau auf eigene Gefahr.

## Warum nicht die AP310-Integration?

Die HACS-Integrationen von Tvalley71 und Goemon64 nutzen **Modbus TCP
Port 502** der Geräte AP190 / AP310 / AP460 (ab ca. 2013, RJ45 auf der
Platine). Die Avent P mit DTH-029255-04 ist die Vorgängergeneration
(Funk-nRF905, optionale Klemmleiste J8, kein Ethernet).

## Credits

- [d00616/P300](https://github.com/d00616/P300) — UART-Weiche, Teensy
- [KNX-User-Forum](https://knx-user-forum.de/forum/%C3%B6ffentlicher-bereich/knx-eib-forum/25323-pluggit-l%C3%BCftungsanlage-anbinden) — Protokoll, nRF905 868,4 MHz
- Pluggit-Betriebsanleitung Avent P300/P450 (2011) — J8 3/6/9 V
