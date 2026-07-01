import argparse
import os
import platform
import sys
import time

# sudo .venv/bin/python3 controller.py

# cd src/python/
# source .venv/bin/activate
# ./.venv/bin/python3 controller.py

if sys.prefix == sys.base_prefix:
    print("Bitte mit dem venv starten: ./.venv/bin/python3 controller.py")
    raise SystemExit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if sys.path and os.path.abspath(sys.path[0]) == SCRIPT_DIR:
    sys.path.pop(0)

try:
    import msvcrt
except ImportError:
    msvcrt = None

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    print("pyserial fehlt. Installiere es mit: pip install pyserial")
    raise SystemExit(1) from exc

DEFAULT_PORT = os.environ.get("CARRERA_SERIAL_PORT", "COM12")


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


def read_key() -> str | None:
    if msvcrt is None:
        return None
    if not msvcrt.kbhit():
        return None

    key = msvcrt.getwch()
    if key in ("\x00", "\xe0"):
        if msvcrt.kbhit():
            msvcrt.getwch()
        return None
    return key


def speed_from_key(key: str) -> int | None:
    if key == "0":
        return 100
    if key.isdigit():
        return int(key) * 10
    return None


def print_help(port: str, baudrate: int) -> None:
    print(f"Verbunden mit {port} @ {baudrate}")
    print("Tasten: 1-9 = 10-90%, 0 = 100%, Leertaste = Spurwechsel, h = Handcontroller, c = Kalibrierung, ESC/q = Ende")


def main() -> int:
    parser = argparse.ArgumentParser(description="Carrera Controller per serieller Schnittstelle")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serieller Port, z. B. COM5")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate, Standard: 115200")
    args = parser.parse_args()

    port = args.port or find_default_port()
    if not port:
        print("Kein serieller Port gefunden. Gib --port COMx an.")
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

    print_help(port, args.baud)

    try:
        while True:
            drain_serial(ser)
            key = read_key()
            if key is None:
                time.sleep(0.02)
                continue

            if key in ("\x1b", "q", "Q"):
                send_command(ser, "h")
                print("Beendet.")
                break

            if key == " ":
                send_command(ser, "l")
                continue

            if key in ("h", "H"):
                send_command(ser, "h")
                continue

            if key in ("c", "C"):
                send_command(ser, "c")
                continue

            speed = speed_from_key(key)
            if speed is not None:
                send_command(ser, f"s:{speed}")
                print(f"Speed: {speed}%")
    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())