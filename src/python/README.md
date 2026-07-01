# Python Serial Controller (controller.py)

Diese Doku beschreibt, wie `controller.py` den ESP32 per serieller Schnittstelle steuert.

## Hardware-Setup (vor dem Software-Start)

1. Adapterplatine in die Carrera Blackbox stecken.
2. Ein Auto mit dem analogen Handregler koppeln (paaren).
3. Analogen Handregler an der Adapterplatine anschliessen.
4. Serielle Verbindung zwischen ESP und Endgeraet herstellen (z. B. per USB-Kabel).

Erst wenn diese Schritte erledigt sind, sollte die serielle Steuerung per `controller.py` gestartet werden.

## Zweck

`controller.py` sendet serielle Textbefehle an den ESP.
Der ESP verarbeitet diese Befehle in der Arduino-Firmware.

## Voraussetzungen

- Windows oder Linux
- Python 3.10+
- USB-Verbindung zum ESP
  - Windows: COM-Port sichtbar (z. B. COM12)
  - Linux: Port sichtbar als `/dev/ttyUSB0` oder `/dev/ttyACM0`
- Python-Paket:
  - `pyserial`

## Installation und Start (Windows PowerShell)

In den Projektordner wechseln:

```powershell
cd C:\Users\silas\Desktop\Studium\TIBSem6\Studienprojekt\Code\src\python
```

Virtuelle Umgebung erstellen und Paket installieren:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install pyserial
```

Starten:

```powershell
.\.venv\Scripts\python.exe controller.py --port COM12 --baud 115200
```

## Installation und Start (Linux)

In den Projektordner wechseln:

```bash
cd /pfad/zum/Code/src/python
```

Virtuelle Umgebung erstellen und Paket installieren:

```bash
python3 -m venv .venv
./.venv/bin/python3 -m pip install pyserial
```

Starten:

```bash
./.venv/bin/python3 controller.py --port /dev/ttyUSB0 --baud 115200
```

Alternative je nach USB-Seriell-Chip:

```bash
./.venv/bin/python3 controller.py --port /dev/ttyACM0 --baud 115200
```


## Tastatursteuerung des Beispiel Skripts

- `1` bis `9` -> 10% bis 90% Geschwindigkeit
- `0` -> 100% Geschwindigkeit
- `Leertaste` -> Spurwechsel
- `h` -> Handcontroller-Modus am ESP
- `c` -> Kalibrierung am ESP starten
- `ESC` oder `q` -> Beenden

## Nachrichtenformat an den ESP

Das Script sendet ASCII-Text mit Zeilenende (`\n`).

- Geschwindigkeit: `s:<wert>`
  - Beispiel: `s:50\n` fuer 50%
- Spurwechsel: `l\n`
- Handcontroller: `h\n`
- Kalibrierung: `c\n`

## Eigene Daten senden (ohne controller.py)

Du kannst die gleichen Kommandos manuell an den seriellen Port senden.
Wichtig ist immer das Zeilenende `\n`.

Beispiele:

- `s:20\n`
- `s:100\n`
- `l\n`
