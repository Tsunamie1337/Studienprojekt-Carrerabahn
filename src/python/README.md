# Webhook Controller (Webhook.py)

Diese Doku beschreibt, wie Webhook.py den ESP32 per HTTP steuert.

## Hardware-Setup (vor dem Software-Start)

1. Adapterplatine in die Carrera Blackbox stecken.
2. Ein Auto mit dem analogen Handregler koppeln.
3. Analogen Handregler an der Adapterplatine anschliessen.
4. ESP mit Strom versorgen und per WLAN erreichbar machen.
5. Endgeraet mit dem ESP-Netzwerk verbinden.

## Zweck

Webhook.py sendet HTTP-Requests an den ESP:

- Geschwindigkeit an /speed
- Spurwechsel an /spurwechsel

Die Tastatur wird live gelesen und daraus werden Requests erzeugt.

## Voraussetzungen

- Python 3.10+
- Python-Pakete:
  - requests
- Terminal mit curses-Unterstuetzung (Linux/WSL empfohlen)

Hinweis fuer Windows:
Das Standard-curses ist unter Windows oft nicht vorhanden. In dem Fall Linux/WSL nutzen oder windows-curses installieren.

## Installation

Im Ordner src/python:

Linux oder WSL:

```bash
sudo pip3 install requests
```

Windows PowerShell:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install requests
```

## Start

Linux oder WSL:

```bash
python3 Webhook.py
```

Windows PowerShell:

```powershell
.\.venv\Scripts\python.exe Webhook.py
```

## Konfiguration

In Webhook.py ist aktuell gesetzt:

- BASE_URL = http://192.168.4.1

Wenn der ESP eine andere IP hat, BASE_URL in src/python/Webhook.py anpassen.

## Bedienung

- 1 bis 9 setzt 10 bis 90 Prozent
- 0 setzt 100 Prozent
- Leertaste loest Spurwechsel aus
- ESC beendet das Programm

Beim Beenden wird 0 Prozent gesendet.

## HTTP-Nachrichtenformat

Webhook.py sendet folgende Requests:

- POST /speed mit JSON Body
  - {"value": 50}
- POST /spurwechsel ohne Body

Beispiele:

- POST http://192.168.4.1/speed
  - Content-Type: application/json
  - Body: {"value": 70}
- POST http://192.168.4.1/spurwechsel

## Troubleshooting

- Timeout oder Verbindungsfehler:
  - Endgeraet ist nicht mit dem ESP-WLAN verbunden.
  - BASE_URL passt nicht.

- 404 bei /speed oder /spurwechsel:
  - Firmware im webhook-Branch ist nicht aktiv oder falsche Firmware geflasht.

- curses Fehler unter Windows:
  - Unter Linux/WSL starten oder windows-curses installieren.
