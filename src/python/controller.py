#!/usr/bin/env python3

import argparse
import os
import platform
import select
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if sys.path and os.path.abspath(sys.path[0]) == SCRIPT_DIR:
    sys.path.pop(0)

try:
    import msvcrt
except ImportError:
    msvcrt = None

if os.name != "nt":
    import termios
    import tty

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    print("pyserial fehlt. Installiere es mit: pip install pyserial")
    raise SystemExit(1) from exc

def get_default_port() -> str:
    env_port = os.environ.get("CARRERA_SERIAL_PORT")
    if env_port:
        return env_port
    return "COM12" if os.name == "nt" else "/dev/ttyUSB0"

class KeyReader:
    def __init__(self) -> None:
        self._fd = None
        self._old_settings = None

    def __enter__(self):
        if os.name != "nt":
            self._fd = sys.stdin.fileno()
            self._old_settings = termios.tcgetattr(self._fd)
            tty.setcbreak(self._fd)
        return self

    def __exit__(self, exc_type, exc, tb):
        if os.name != "nt" and self._fd is not None and self._old_settings is not None:
            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old_settings)

    def read_key(self) -> str | None:
        if os.name == "nt":
            if msvcrt is None or not msvcrt.kbhit():
                return None
            key = msvcrt.getwch()
            if key in ("\x00", "\xe0"):
                if msvcrt.kbhit():
                    msvcrt.getwch()
                return None
            return key

        ready, _, _ = select.select([sys.stdin], [], [], 0)
        if not ready:
            return None
        return sys.stdin.read(1)

def running_in_wsl() -> bool:
    return "microsoft" in platform.release().lower() or "WSL_DISTRO_NAME" in os.environ

def find_default_port() -> str | None:
    ports = list(list_ports.comports())
    if not ports:
        return None
    return ports[0].device

def open_serial(port: str, baudrate: int):
    return serial.Serial(port=port, baudrate=baudrate, timeout=0, write_timeout=1)

# send_command() baut jetzt ausschließlich valide Frames
def send_frame(ser, frame: str) -> None:
    """Sendet einen Frame als '<frame>\n' ohne zusätzliches strip/encode-Overhead."""
    ser.write((frame + "\n").encode())
    ser.flush()

def send_speed(ser, speed: int) -> None:
    """Sendet Geschwindigkeits-Frame: V000 – V100"""
    speed = max(0, min(100, speed))
    send_frame(ser, f"V{speed:03d}")

def send_lane(ser) -> None:
    """Sendet Lane-Switch-Frame: L"""
    send_frame(ser, "L")

def send_mode_controller(ser) -> None:
    send_frame(ser, "C")

def send_mode_software(ser) -> None:
    send_frame(ser, "S")

def send_calibrate(ser) -> None:
    send_frame(ser, "K")

# drain_serial() filtert '#'-Kommentarzeilen und wertet nur 'V'-Frames und 'L'-Frames aus           
def drain_serial(ser) -> None:
    while ser.in_waiting:
        raw = ser.readline().decode("utf-8", errors="replace").strip()
        if not raw:
            continue

        if raw.startswith("#"):
            # Heartbeat / Statuszeile – nur anzeigen, nicht parsen
            print(f"ESP: {raw}")
            continue

        if raw.startswith("V") and len(raw) == 4:
            try:
                speed = int(raw[1:])
                print(f"ESP Speed: {speed}%")
            except ValueError:
                pass
            continue

        if raw == "L":
            print("ESP: Spurwechsel ausgelöst")
            continue

        # Unbekannte Frames ignorieren
        print(f"ESP (unbekannt): {raw}")

#
def speed_from_key(key: str) -> int | None:
    if key == "0":
        return 100
    if key.isdigit():
        return int(key) * 10
    return None

def print_help(port: str, baudrate: int, mode: str) -> None:
    print(f"Verbunden mit {port} @ {baudrate}")
    print(f"Modus: {mode}")
    print("Tasten: 1-9=10-90%, 0=100%, Leertaste=Spurwechsel, "
          "c=Controller, s=Software, k=Kalibrierung, ESC/q=Ende")

def main() -> int:
    parser = argparse.ArgumentParser(description="Carrera Controller")
    parser.add_argument("--port", help="Serieller Port")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    port = args.port or get_default_port() or find_default_port()
    if not port:
        print("Kein serieller Port gefunden.")
        return 1

    if running_in_wsl() and port.upper().startswith("COM"):
        available = ", ".join(d.device for d in list_ports.comports()) or "/dev/ttyS0-3"
        print(f"{port} unter WSL nicht verfügbar. Verfügbare Ports: {available}")
        return 1

    try:
        ser = open_serial(port, args.baud)
    except serial.SerialException as exc:
        print(f"Konnte {port} nicht öffnen: {exc}")
        return 1

    active_mode = "software"
    print_help(port, args.baud, active_mode)

    try:
        with KeyReader() as key_reader:
            # Initialer Befehl als Frame
            send_mode_software(ser)

            while True:
                drain_serial(ser)
                key = key_reader.read_key()
                if key is None:
                    time.sleep(0.02)
                    continue

                if key in ("\x1b", "q", "Q"):
                    send_speed(ser, 0)
                    print("Beendet.")
                    break

                if key == " ":
                    send_lane(ser)
                    continue

                if key in ("c", "C", "h", "H"):
                    send_mode_controller(ser)
                    active_mode = "controller"
                    print("Modus: controller")
                    continue

                if key in ("s", "S"):
                    send_mode_software(ser)
                    active_mode = "software"
                    print("Modus: software")
                    continue

                if key in ("k", "K"):
                    send_calibrate(ser)
                    continue

                speed = speed_from_key(key)
                if speed is not None:
                    if active_mode == "software":
                        send_speed(ser, speed)
                        print(f"Speed: {speed}%")
                    else:
                        print("Taste ignoriert – Controller-Modus aktiv.")
    finally:
        ser.close()

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
