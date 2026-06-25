import curses
import time

import requests

BASE_URL = "http://192.168.4.1"
# sudo .venv/bin/python3 webhook.py

def set_speed(percent: int):
    """Sendet Geschwindigkeit an ESP32"""
    try:
        r = requests.post(f"{BASE_URL}/speed", json={"value": percent}, timeout=0.5)
        print(f"Speed: {percent}%  → {r.status_code}")
    except requests.exceptions.RequestException as e:
        print(f"Fehler: {e}")

def spurwechsel():
    """Löst Spurwechsel aus"""
    try:
        r = requests.post(f"{BASE_URL}/spurwechsel", timeout=0.5)
        print(f"Spurwechsel → {r.status_code}")
    except requests.exceptions.RequestException as e:
        print(f"Fehler: {e}")

def run_controller(screen):
    curses.curs_set(0)
    screen.nodelay(True)
    screen.keypad(True)
    screen.clear()
    screen.refresh()

    print("Carrera Controller bereit!")
    print("Zifferntasten 1-9: 10%-90% | 0: 100% | Leertaste: Spurwechsel | ESC: Beenden")
    print()

    aktueller_speed = 0
    letzter_wechsel = 0.0
    letzter_gesendeter_speed = None

    set_speed(aktueller_speed)

    while True:
        # Aktuelle Geschwindigkeit anzeigen
        speed_bar = "█" * (aktueller_speed // 10) + "░" * ((100 - aktueller_speed) // 10)
        screen.addstr(5, 0, f"Speed: {aktueller_speed:3d}% [{speed_bar}]", curses.A_BOLD)
        screen.refresh()

        taste = screen.getch()

        # Wenn eine Zahl gedrückt wird: 1..9 -> 10%..90%, 0 -> 100%
        if taste in (ord(str(n)) for n in range(0, 10)):
            char = chr(taste)
            if char == '0':
                aktueller_speed = 100
            else:
                aktueller_speed = int(char) * 10

        # Leertaste: Spurwechsel (debounced)
        elif taste == ord(' '):
            jetzt = time.time()
            if (jetzt - letzter_wechsel) > 0.5:
                spurwechsel()
                letzter_wechsel = jetzt

        # ESC: Beenden
        elif taste == 27:
            aktueller_speed = 0
            if letzter_gesendeter_speed != 0:
                set_speed(0)
            screen.addstr(7, 0, "Beendet.")
            screen.refresh()
            break

        # Kein Tastendruck: kein Speed (0)
        elif taste == -1:
            aktueller_speed = 0

        # Nur senden, wenn sich der Wert geändert hat
        if aktueller_speed != letzter_gesendeter_speed:
            set_speed(aktueller_speed)
            letzter_gesendeter_speed = aktueller_speed

        time.sleep(0.05)


def main():
    curses.wrapper(run_controller)


if __name__ == "__main__":
    main()