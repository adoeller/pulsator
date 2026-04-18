# Power Interrupt Tester

**Sprache / Language:** [Deutsch](#deutsch) · [English](#english)

---

## Deutsch

### Beschreibung

**Power Interrupt Tester** ist eine externe App (`.fap`) für den Flipper Zero mit Momentum Firmware. Sie dient zum Testen von Interrupt-Logik und Flankenverhalten externer Schaltungen, indem sie zwei Ausgangspins für eine präzise einstellbare Zeit in den invertierten Zustand versetzt.

### Ausgänge

Alle drei Ausgänge schalten synchron — ein einziger Funktionsaufruf steuert alle gleichzeitig:

| Pin | Spannung | Beschreibung |
|-----|----------|--------------|
| Pin 1 | +5 V (OTG) | Schaltbarer 5V-Ausgang über den internen Boost-Converter |
| Pin 7 / PC3 | 3,3 V (GPIO) | Direkter Push-Pull-GPIO-Ausgang |
| Pin 8 / 18 | GND | Masse-Referenz |
| Interne LED (blau) | — | Spiegelt den aktuellen Signalzustand sichtbar |

### Startwerte

| Parameter | Startwert | Minimum | Maximum |
|-----------|-----------|---------|---------|
| Pulsdauer | 100 ms | 1 ms | 600.000 ms (10 min) |
| Delta | 10 ms | 1 ms | 10.000 ms |
| Modus | ON (5V aktiv) | — | — |

### Bedienung

#### Intro-Screen
Beim Start zeigt die App die Pin-Belegung und eine kurze Tastenbelegung.

| Taste | Funktion |
|-------|----------|
| `OK` | App starten, Ausgänge einschalten |
| `BACK` | App beenden |

#### Haupt-Screen

| Taste | Funktion |
|-------|----------|
| `OK` (kurz) | Impuls auslösen — alle Ausgänge für die eingestellte Pulsdauer invertieren |
| `OK` (lang) | Modus dauerhaft invertieren — schaltet den Default-Zustand aller Ausgänge von ON auf OFF oder umgekehrt |
| `↑` | Pulsdauer um Delta erhöhen |
| `↓` | Pulsdauer um Delta verringern |
| `→` | Delta × 10 |
| `←` | Delta ÷ 10 |
| `BACK` | App beenden, alle Ausgänge abschalten |

### Anzeige

```
Power Interrupt Tester
────────────────────────────
Mode: ON        Pin: HIGH
Puls: 100ms     Delta: 10ms

OK:puls  hold OK:mode
U/D:dur  L/R:Delta/10*10
```

- **Mode:** Zeigt den Default-Zustand der Ausgänge (`ON` oder `OFF`)
- **Pin:** Zeigt den aktuellen Pegel (`HIGH` oder `LOW`), während eines Impulses entsprechend invertiert
- **Puls:** Eingestellte Impulsdauer — Werte ≥ 1 s werden als `1s`, `1.5s` usw. angezeigt
- **Delta:** Schrittweite für Pulsdaueränderung
- **Punkt (●):** Kleiner Indikator im Titelbereich, leuchtet während ein Impuls aktiv ist
- **Toast:** Kurze Einblendung bei Moduswechsel

### Zeitgenauigkeit

Die Impulsdauer basiert auf dem FreeRTOS-Ticker (1 ms Auflösung). Für Pulse ab ca. 50 ms ist der relative Fehler unter 6 %. Für sehr kurze Pulse (< 10 ms) überwiegt der systembedingte Jitter von ±1–3 ms. Microsekunden-Pulse sind mit dieser Architektur nicht darstellbar.

### Build

```bash
# Projektordner in den ufbt-Workspace legen
ufbt
# Ausgabe: dist/pulsator.fap
```

Benötigt: `ufbt`, Momentum Firmware SDK.

---

## English

### Description

**Power Interrupt Tester** is an external app (`.fap`) for the Flipper Zero running Momentum Firmware. It is designed to test interrupt logic and edge-triggered behaviour of external circuits by toggling two output pins to their inverted state for a precisely adjustable duration.

### Outputs

All three outputs switch synchronously — a single function call controls all of them at once:

| Pin | Voltage | Description |
|-----|---------|-------------|
| Pin 1 | +5 V (OTG) | Switchable 5 V output via the internal boost converter |
| Pin 7 / PC3 | 3.3 V (GPIO) | Direct push-pull GPIO output |
| Pin 8 / 18 | GND | Ground reference |
| Internal LED (blue) | — | Mirrors the current signal state visibly |

### Default Values

| Parameter | Default | Minimum | Maximum |
|-----------|---------|---------|---------|
| Pulse duration | 100 ms | 1 ms | 600,000 ms (10 min) |
| Delta | 10 ms | 1 ms | 10,000 ms |
| Mode | ON (5 V active) | — | — |

### Controls

#### Intro Screen
On launch the app displays the pin assignment and a brief button reference.

| Button | Action |
|--------|--------|
| `OK` | Start the app, turn outputs on |
| `BACK` | Exit the app |

#### Main Screen

| Button | Action |
|--------|--------|
| `OK` (short) | Trigger a pulse — inverts all outputs for the set pulse duration |
| `OK` (long) | Permanently invert the mode — toggles the default state of all outputs between ON and OFF |
| `↑` | Increase pulse duration by delta |
| `↓` | Decrease pulse duration by delta |
| `→` | Delta × 10 |
| `←` | Delta ÷ 10 |
| `BACK` | Exit the app, turn all outputs off |

### Display

```
Power Interrupt Tester
────────────────────────────
Mode: ON        Pin: HIGH
Puls: 100ms     Delta: 10ms

OK:puls  hold OK:mode
U/D:dur  L/R:Delta/10*10
```

- **Mode:** Shows the default state of the outputs (`ON` or `OFF`)
- **Pin:** Shows the current signal level (`HIGH` or `LOW`), inverted during an active pulse
- **Puls:** Configured pulse duration — values ≥ 1 s are displayed as `1s`, `1.5s`, etc.
- **Delta:** Step size for pulse duration adjustments
- **Dot (●):** Small indicator in the title area, lit while a pulse is active
- **Toast:** Brief overlay shown when the mode is toggled

### Timing Accuracy

Pulse duration is based on the FreeRTOS tick timer (1 ms resolution). For pulses of around 50 ms and above, the relative timing error stays below 6 %. For very short pulses (< 10 ms), the system-level jitter of ±1–3 ms dominates. Microsecond-range pulses are not achievable with this architecture.

### Build

```bash
# Place the project folder into the ufbt workspace
ufbt
# Output: dist/pulsator.fap
```

Requires: `ufbt`, Momentum Firmware SDK.
