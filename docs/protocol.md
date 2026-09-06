# Protokollnotizen Avent P / P300 / P450

Quellen: Betriebsanleitung 2011, KNX-User-Forum Thread
„Pluggit Lüftungsanlage anbinden“, GitHub d00616/P300.

## Funkstrecke Fernbedienung ↔ Gerät

- Chip: Nordic **nRF905**
- Frequenz: **868,4 MHz**
- Modulation: GFSK / ShockBurst
- Payload: 32 Byte
- Reichweite laut Handbuch: ca. 30 m durch Wände
- Die FB sendet nur nach Tastendruck (kein Dauerfunk, Sleep nach 2 min)

Beispiel-Sniff (Forum):

```
FB : 90800304 00000CDB F9FB3FE1 E9CAEF69 5FBFFFE3 FD9FFFDE BF6D3F1D B7257F69
KWL: 80900318 02100815 0C0A7A00 00000000 00041610 07101207 504C0000 FFFFFFFF
```

Die Geräteadresse ist in den ersten Bytes und wird beim Pairing
festgeschrieben. Eine zweite FB muss an die bestehende Platine
angelernt werden (Handbuch: „zusätzliche Fernbedienung“).

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

Für diese Daten: UART-Weiche oder Funk-Replay nach Sniff.
