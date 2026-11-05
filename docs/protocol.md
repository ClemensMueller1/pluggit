# Protokollnotizen Avent P / P300 / P450

Quellen: Betriebsanleitung 2011, KNX-User-Forum Thread
„Pluggit Lüftungsanlage anbinden“, GitHub d00616/P300.

## Funkstrecke Fernbedienung ↔ Gerät

- Chip: Nordic **nRF905**
- Frequenz: **868,4 MHz**
  (`CH_NO=0x76`, `HFREQ_PLL=1` → Config-Byte 1 = **0x0E**, nicht 0x0C.
  0x0C ist 434,2 MHz.)
- Modulation: GFSK / ShockBurst, 50 kBd
- Payload: 32 Byte, CRC-16, 16-MHz-Quarz
- ShockBurst-Adresse: **`00 00 xx xx`** (nicht Nordic-Default `E7 E7 E7 E7`).
  Die letzten zwei Bytes stehen in den Modbus-Registern 5 und 6 der
  Anlage (KNX-User-Forum #218, Beispiel `0A 7A` → Adresse `00 00 0A 7A`).
- Reichweite laut Handbuch: ca. 30 m. Billige PTR8000-Module oft nur
  **~20 cm**, bis ein 868-MHz-optimiertes Modul da ist.
- Die FB sendet nur nach Tastendruck (kein Dauerfunk, Sleep nach 2 min)

Die ESPHome-Komponente startet im Replay-Modus (gespeicherte Adresse,
CRC-16) und loggt `NVM ShockBurst address: …`. Der nRF905 sendet die
Adressbytes LSB zuerst: `11 22 33 44` geht als `44 33 22 11` auf die
Luft, `00 00 2B 9F` als `9F 2B 00 00`. Sniff nur über den Button
„RF Adresse neu lernen“: CRC aus, TX- und RX-Adresslänge 0
(Config-Byte 2 = `0x00`, Readback muss das bestätigen). Beim Start
sendet der ESP einmal das Link-Test-Muster. Ein Pluggit-Rahmen kann
im Puffer mit den vier Adressbytes in Luftreihenfolge beginnen und
danach `80 90 03 18` enthalten. Address 05 und 06 darin sind die
Geräteadresse (`00 00` plus diese zwei Bytes). Der Sniff loggt sie
und endet erst, wenn die Adresse in der HA-Oberfläche gesetzt wird
(`rf/address/set`, danach NVM, CRC-16, Adressbreite 4). Aus/1/2/3-Pakete
in der HA-MQTT-Config ablegen; die Fan-Entität sendet sie auf `rf/tx`.

Beispiel-Sniff (Forum):

```
FB : 90800304 00000CDB F9FB3FE1 E9CAEF69 5FBFFFE3 FD9FFFDE BF6D3F1D B7257F69
KWL: 80900318 02100815 0C0A7A00 00000000 00041610 07101207 504C0000 FFFFFFFF
```

`90…` kommt von der Fernbedienung, `80…` von der Anlage. Eine zweite FB
muss an die bestehende Platine angelernt werden (Handbuch:
„zusätzliche Fernbedienung“).

## Lüfterstufe und T1–T4 im Funkrahmen

Die FB liest nach dem Aufwachen Holding Register ab `0x0400`
(12 Register, im Payload byteweise). Die fünf Werte stehen nur in der
Antwort der Anlage, und nur wenn die vorige Abfrage `0x0400` war.
`0x040C` hat denselben Kopf `80 90 03 18`, enthält aber andere Register
(Vorheizregister, Kalibrierung) und wird verworfen.

```
FB : 90 80 03 04 00 00 0C …          FC03, Start 0x0400, 12 Register
KWL: 80 90 03 18 SS …… AA AA …… T1 T2 T3 T4 ……
```

| Offset | Bedeutung |
|---|---|
| 4 | Lüfterstufe `SS` (0–3) |
| 9–10 | `AA AA`, die letzten zwei Bytes der ShockBurst-Adresse |
| 20 | T1 Außenluft, °C |
| 21 | T2 Zuluft, °C |
| 22 | T3 Abluft, °C |
| 23 | T4 Fortluft, °C |

Beispiel aus dem Mitschnitt: Stufe 1, Adresse `2B 9F`, 10 / 19 / 20 / 11 °C.
Ein Schreibrahmen `90 80 10 00 00 00 01 02 00 SS` setzt die Stufe sofort;
Temperaturen kommen erst mit der nächsten Leseantwort.

Der ESP sendet die mitgeschnittene `0x0400`-Abfrage etwa 15 s nach dem
Start und danach alle 10 Minuten, zusätzlich zu dem, was die FB selbst
sendet. Die Werte gehen retained auf `speed`, `t1`, `t2`, `t3`, `t4`.

## UART Funkmodul ↔ Hauptplatine

- 4800 Baud, 8N1, 3,3 V
- Modbus-RTU-ähnlich, Slave 1
- Funktion 0x03 Lesen, 0x10 Schreiben
- Beispiel Leseanfrage: `01 03 04 00 00 0C …`

Temperaturabfragen (Forum, Byte 5 = Register):

| Byte | Bedeutung |
|---|---|
| 0x01 | T1 Außenluft |
| 0x02 | T2 Zuluft |
| 0x03 | T3 Abluft |
| 0x04 | T4 Fortluft |

Die UART-Register der P300 sind **nicht** die Modbus-TCP-Register der
späteren AP190/AP310 (40133 T1, 40325 Lüfterstufe, …). Die ESPHome-UART-
Firmware nutzt die P300-Adressen und muss ggf. an den eigenen
Softwarestand angepasst werden (Aufkleber am Funkmodul, Ziel 03.08.01).

## Offizielle analoge / digitale Steuerung

Unabhängig vom Funkprotokoll und ohne Reverse Engineering nutzbar.
Siehe [verdrahtung.md](verdrahtung.md). Das ist der unterstützte Weg
für Home Assistant.

## Was die Relais- / Analog-Anbindung **nicht** kann

- Temperaturen T1–T4 auslesen
- Filteralarm der FB anzeigen (nur wenn J3 verdrahtet oder UART)
- Wochenprogramm der FB schreiben
- Bypass gezielt öffnen
- Standby Stufe 0 (nur über die FB; Analog 0 V gibt die Kontrolle
  an die FB zurück, schaltet aber nicht zwingend auf 0)

Für diese Daten: UART-Weiche, oder über Funk Lüfterstufe und T1–T4
aus der Antwort auf Register `0x0400`.
