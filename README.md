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

Jeder Branch hat eine eigene Firmware-Variante mit eigenem `.cpp`-Skript fuer den ESP.

Zusaetzlich gilt:

- Die Branches `serial` und `webhook` enthalten jeweils ein Python-Skript zur Steuerung.
- In diesen beiden Branches liegt beim jeweiligen Python-Skript eine eigene README mit den branch-spezifischen Start- und Bedienhinweisen.

Hinweis fuer den aktuellen Branch:

- Firmware liegt in [src/arduino/main.cpp](src/arduino/main.cpp)
- Python-Doku liegt in [src/python/README.md](src/python/README.md)

## Projektueberblick

Das Projekt liest die Gasstellung eines Handreglers analog ein und bildet sie auf ein PWM-Signal ab.
Das PWM-Signal wird so skaliert, dass die effektive Ausgangsspannung auf ca. 2.0 V begrenzt wird (bezogen auf 3.3 V Referenz).

Wichtige Logik:
- ADC-Eingang: Gaswert am Pin `PIN_GAS`
- Kalibrierte Min/Max-Werte: `adc_kein_gas` und `adc_vollgas`
- Normalisierung mit Deadzone
- Mapping auf PWM-Bereich bis `PWM_MAX_2V`

## Hardware (laut aktuellem Code)

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


## Serial Monitor verwenden

In [platformio.ini](platformio.ini) ist der Monitor auf 115200 Baud eingestellt.

Start per Terminal:

```bash
pio device monitor -b 115200
```

Oder in VS Code den PlatformIO Serial Monitor starten.

Beim Start meldet die Firmware unter anderem:
- `Carrera Controller bereit.`
- aktuelle Kalibrierwerte
- Hinweis: `Kalibrieren: 'c' senden`

## Hauptfunktionen erklaert

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
- max. PWM auf einen Zielwert von 2.0 V skaliert (`PWM_MAX_2V`)

So folgt die Bahnsteuerung direkt der Trigger-Stellung.

### 3) Spurwechsel-Button

Der Spurwechsel-Eingang wird entprellt und mit Cooldown verarbeitet:
- aktiv bei LOW (gedrueckt)
- Cooldown verhindert Mehrfachtrigger
- Ausgang fuer Transistor wird invertiert geschaltet (wie im Code kommentiert)

## Kalibrierungsmodus

Es sind Werte für den Handregler vordefiniert, aber diese können je nach Setup nicht komplett stimmen daher gibt 

Kalibrierung startet ueber den Serial Monitor mit:

```text
c
```

Ablauf:
1. Trigger loslassen und mit `y` bestaetigen.
2. Trigger voll druecken und mit `y` bestaetigen.
3. Gemessene Werte werden angezeigt.
4. Mit `y` uebernehmen oder mit `n` wiederholen.

Wenn die Spreizung zwischen Min/Max zu klein ist, wird die Kalibrierung verworfen und muss wiederholt werden.
