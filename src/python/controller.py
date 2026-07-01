#!/usr/bin/env python3

import argparse
import os
import platform
import select
import sys
import time

# Optional setup
# Windows:
#   py -m venv .venv
#   .\.venv\Scripts\python.exe -m pip install pyserial
#   .\.venv\Scripts\python.exe controller.py --port COM12
# Linux:
#   python3 -m pip install --user pyserial
#   python3 controller.py --port /dev/ttyUSB0
#
# A virtual environment still works, but it is no longer required.

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


def send_command(ser, command: str) -> None:
    line = (command.strip() + "\n").encode("utf-8")
    ser.write(line)
    ser.flush()


def drain_serial(ser) -> None:
    while ser.in_waiting:
        raw = ser.readline().decode("utf-8", errors="replace").strip()
        if raw:
            print(f"ESP: {raw}")


def speed_from_key(key: str) -> int | None:
    if key == "0":
        return 100
    if key.isdigit():
        return int(key) * 10
    return None


def print_help(port: str, baudrate: int, mode: str) -> None:
    print(f"Verbunden mit {port} @ {baudrate}")
    print(f"Modus: {mode}")
    print("Tasten: 1-9 = 10-90%, 0 = 100%, Leertaste = Spurwechsel, c = Controller, s = Software, k = Kalibrierung, ESC/q = Ende")


def main() -> int:
    parser = argparse.ArgumentParser(description="Carrera Controller per serieller Schnittstelle")
    parser.add_argument("--port", help="Serieller Port, z. B. COM12, /dev/ttyUSB0 oder /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate, Standard: 115200")
    args = parser.parse_args()

    port = args.port or get_default_port() or find_default_port()
    if not port:
        if os.name == "nt":
            print("Kein serieller Port gefunden. Gib --port COMx an.")
        else:
            print("Kein serieller Port gefunden. Gib --port /dev/ttyUSB0 oder /dev/ttyACM0 an.")
        return 1

    if running_in_wsl() and port.upper().startswith("COM"):
        available_ports = ", ".join(device.device for device in list_ports.comports())
        if not available_ports:
            available_ports = "/dev/ttyS0 bis /dev/ttyS3"
        print(f"{port} ist unter WSL nicht direkt als Windows-COM-Port verfügbar. Starte das Script unter Windows oder verwende einen in WSL sichtbaren Port. Verfügbare Ports: {available_ports}")
        return 1

    try:
        ser = open_serial(port, args.baud)
    except serial.SerialException as exc:
        print(f"Konnte {port} nicht oeffnen: {exc}")
        return 1

    active_mode = "software"
    print_help(port, args.baud, active_mode)

    try:
        with KeyReader() as key_reader:
            send_command(ser, "s")
            while True:
                drain_serial(ser)
                key = key_reader.read_key()
                if key is None:
                    time.sleep(0.02)
                    continue

                if key in ("\x1b", "q", "Q"):
                    send_command(ser, "s")
                    print("Beendet.")
                    break

                if key == " ":
                    send_command(ser, "l")
                    continue

                if key in ("c", "C", "h", "H"):
                    send_command(ser, "c")
                    active_mode = "controller"
                    print("Modus: controller")
                    continue

                if key in ("s", "S"):
                    send_command(ser, "s")
                    active_mode = "software"
                    print("Modus: software")
                    continue

                speed = speed_from_key(key)
                if speed is not None:
                    if active_mode == "software":
                        send_command(ser, f"s:{speed}")
                        print(f"Speed: {speed}%")
                    else:
                        print("Taste ignoriert im Controller-Modus. Mit 's' wieder Software aktivieren.")
    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())