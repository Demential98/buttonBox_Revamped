#!/usr/bin/env python3
import sys, pathlib, time

try:
    import serial  # pyserial
except Exception:
    serial = None


def send_json(port: str, json_path: str, baud: int = 115200):
    if serial is None:
        print("pyserial not installed. Run:  pip install pyserial")
        sys.exit(1)

    text = pathlib.Path(json_path).read_text(encoding='utf-8')
    data = text.encode('utf-8')
    ln = len(data)

    with serial.Serial(port, baud, timeout=2) as s:
        time.sleep(0.6)  # allow reset/enumeration
        try:
            banner = s.read(128)
            if banner:
                try: print(banner.decode(errors='ignore').strip())
                except: pass
        except: pass

        cmd = f"PUT {ln}\n".encode('ascii')
        s.write(cmd)
        time.sleep(0.05)
        s.write(data)
        time.sleep(0.05)

        out = s.read(512).decode(errors='ignore')
        print(out.strip() or "(no response)")


def get_json(port: str, baud: int = 115200):
    if serial is None:
        print("pyserial not installed. Run:  pip install pyserial")
        sys.exit(1)
    with serial.Serial(port, baud, timeout=2) as s:
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"GET\n")
        time.sleep(0.2)
        out = s.read(8192).decode(errors='ignore')
        print(out)


def main():
    if len(sys.argv) < 3:
        print("Usage:")
        print("  python put_config.py COMx config.json         # upload")
        print("  python put_config.py COMx --get               # fetch current")
        sys.exit(1)

    port = sys.argv[1]
    if sys.argv[2] == '--get':
        get_json(port)
    else:
        send_json(port, sys.argv[2])


if __name__ == "__main__":
    main()

