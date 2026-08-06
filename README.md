# Studienprojekt-Carrerabahn

ESP32-C3 Controller fuer eine Carrera-Bahn mit:
- ADC-Auslesen eines Handreglers (Gas)
- Umrechnung auf PWM-Ausgabe fuer die Bahn
- Spurwechsel-Button mit Entprellung und Cooldown
- Kalibrierungsmodus ueber den Serial Monitor

## Branch-Ueberblick

Dieses Repository wird in drei Branches gepflegt:

- `serial`
- `webhook`
- `HandcontrollerOnly`

Jeder Branch hat eine eigene Firmware-Variante mit eigenem `.cpp`-Skript fuer den ESP32-C3.

Zusaetzlich gilt:

- Die Branches `serial` und `webhook` enthalten jeweils ein Python-Skript zur Steuerung.
- In diesen beiden Branches liegt beim jeweiligen Python-Skript eine eigene README mit den branch-spezifischen Start- und Bedienhinweisen.
- Im `serial`-Branch gibt es jetzt zwei umschaltbare Modi:
	- `C` = Controller-Modus, Handregler steuert das Auto direkt
	- `S` = Software-Modus, das Python-Skript steuert das Auto per Tastatur

## Projektueberblick


Das Projekt liest entweder die Gasstellung eines Handreglers analog ein und bildet sie auf ein PWM-Signal ab. Andererseits kann man per Skript Steuerbefehle an das Fahrzeug senden.
Das PWM-Signal wird so skaliert, dass die effektive Ausgangsspannung auf ca. 2.0 V begrenzt wird (bezogen auf 3.3 V Referenz).

## Hardware

- Board: ESP32-C3 (PlatformIO Env: `esp32c3` oder `esp32 super mini`)
- Gaspoti/Trigger (ADC): GPIO 3
- Spurwechsel-Button Eingang: GPIO 0 (aktiv LOW)
- PWM-Ausgang zur Bahn: GPIO 4
- Transistorsteuerung Spurwechsel: GPIO 6

## Voraussetzungen

- Visual Studio Code
- PlatformIO Extension
- USB-Verbindung zum ESP32-C3

## Mit PlatformIO bauen und hochladen

### In VS Code (einfachster Weg)

1. Projektordner in VS Code oeffnen.
2. In der PlatformIO-Leiste `Build` ausfuehren.
3. Danach `Upload` ausfuehren.

### Per Terminal

Im Projektordner:

```bash
pio run
pio run -t upload
```
## Hauptfunktionen erklaert

### 0) Zwei Steuerungsmodi

Die Firmware unterscheidet nun zwischen zwei Modi:

- Controller-Modus: Der angeschlossene Handregler steuert das Auto.
- Software-Modus: Ein vordefiniertes Python-Skript steuert das Auto.

Das Python-Skript schaltet mit `C` und `S` zwischen den Modi um.
Im Controller-Modus sendet die Firmware die Fahrdaten zusaetzlich seriell zum Python-Skript im Format `S<0-100>`.

### 1) Handregler per ADC auslesen

Die Funktion zur Gasbestimmung liest den ADC-Wert und normiert ihn auf 0 bis 100 %:
- Rohwert lesen
- auf kalibrierten Bereich (`adc_kein_gas` bis `adc_vollgas`) abbilden
- Deadzone anwenden
- auf Prozentwert umrechnen

Damit werden Rauschen und kleine Nullpunktabweichungen reduziert.

### 2) Verarbeitung zu PWM-Ausgabe

Im Hauptloop wird der Gas-Prozentwert auf den PWM-Wert gemappt und ausgegeben:
- PWM mit 12 Bit Aufloesung
- Frequenz aktuell 5 kHz
- max. PWM auf einen Zielwert von 2.0 V skaliert (`PWM_MAX_2V`). NICHT HÖHER EINSTELLEN! Die Carrerabahn könnte dadurch beschädigt werden.

So folgt die Bahnsteuerung direkt der Trigger-Stellung.

### 3) Spurwechsel-Button

Der Spurwechsel-Eingang wird entprellt und mit Cooldown verarbeitet:
- aktiv bei LOW (gedrueckt)
- Cooldown verhindert Mehrfachtrigger
- Ausgang fuer Transistor wird invertiert geschaltet

## Kalibrierungsmodus

Es sind Werte für den Handregler vordefiniert, aber diese können je nach Setup nicht komplett stimmen daher gibt

Kalibrierung startet ueber den Serial Monitor mit:

```text
K
```

Ablauf:
1. Trigger loslassen und mit `y` bestaetigen.
2. Trigger voll druecken und mit `y` bestaetigen.
3. Gemessene Werte werden angezeigt.
4. Mit `y` uebernehmen oder mit `n` wiederholen.

Wenn die Spreizung zwischen Min/Max zu klein ist, wird die Kalibrierung verworfen und muss wiederholt werden.
