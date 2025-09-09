#!/usr/bin/env python3
import sys, json, struct, pathlib, time

# Optional: pip install pyserial
try:
    import serial  # pyserial
except Exception:
    serial = None

MAGIC   = 0x42434647  # 'BCFG'
VERSION = 1

KEY_ORDER = ['0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F']
ENC_ORDER = ['A+','A-','B+','B-']
MODE_KEYS = ['0','1','2','3']

def build_packed_from_json(cfg: dict) -> bytes:
    # Collect unique actions in stable, first-seen order
    dict_list = []
    index = {}

    def idx_of(action: str) -> int:
        if action not in index:
            if len(dict_list) >= 128:
                raise ValueError("Too many unique actions (>127).")
            index[action] = len(dict_list)
            dict_list.append(action)
        return index[action]

    # Make 4x16 K table, 4x4 E table (both uint8, 0xFF for missing)
    K = [[0xFF]*16 for _ in range(4)]
    E = [[0xFF]*4  for _ in range(4)]

    keys_section = cfg.get("keys", {})
    enc_section  = cfg.get("encoders", {})

    # Fill keys
    for m_i, m in enumerate(MODE_KEYS):
        mm = keys_section.get(m, {})
        for k_i, k in enumerate(KEY_ORDER):
            if k in mm:
                a = mm[k]
                K[m_i][k_i] = idx_of(a)

    # Fill encoders
    for m_i, m in enumerate(MODE_KEYS):
        mm = enc_section.get(m, {})
        for e_i, e in enumerate(ENC_ORDER):
            if e in mm:
                a = mm[e]
                E[m_i][e_i] = idx_of(a)

    # Build string pool (UTF-8, NUL-terminated)
    pool = b''.join(s.encode('utf-8') + b'\x00' for s in dict_list)

    # Header: uint32 magic, uint16 version, uint16 dictCount (LE)
    header = struct.pack('<IHH', MAGIC, VERSION, len(dict_list))

    # Flatten K and E
    k_bytes = bytes(b for row in K for b in row)
    e_bytes = bytes(b for row in E for b in row)

    packed = header + k_bytes + e_bytes + pool
    return packed

def cmd_pack(in_json: str, out_bin: str):
    cfg = json.loads(pathlib.Path(in_json).read_text(encoding='utf-8'))
    data = build_packed_from_json(cfg)
    pathlib.Path(out_bin).write_bytes(data)
    print(f"Packed -> {out_bin}")
    print(f"  dict entries : {struct.unpack('<H', data[6:8])[0]}")
    print(f"  total bytes  : {len(data)}")

def cmd_send(port: str, in_json: str, baud: int = 115200):
    if serial is None:
        print("pyserial not installed. Run:  pip install pyserial")
        sys.exit(1)
    cfg = json.loads(pathlib.Path(in_json).read_text(encoding='utf-8'))
    data = build_packed_from_json(cfg)
    with serial.Serial(port, baud, timeout=2) as s:
        time.sleep(0.6)  # allow 32u4 to reset/enumerate
        # flush banner
        try:
            banner = s.read(128)
            if banner:
                try: print(banner.decode(errors='ignore').strip())
                except: pass
        except: pass
        cmd = f"BINCFG {len(data)}\n".encode()
        s.write(cmd)
        time.sleep(0.05)
        s.write(data)
        time.sleep(0.05)
        # Read a couple of lines back (READY/SAVED/etc.)
        out = s.read(256).decode(errors='ignore')
        print(out.strip() or "(no response)")

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python packcfg.py pack <config.json> <out.bin>")
        print("  python packcfg.py send <COMx> <config.json> [baud]")
        sys.exit(1)

    if sys.argv[1] == "pack" and len(sys.argv) >= 4:
        cmd_pack(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "send" and len(sys.argv) >= 4:
        baud = int(sys.argv[4]) if len(sys.argv) >= 5 else 115200
        cmd_send(sys.argv[2], sys.argv[3], baud)
    else:
        print("Bad args.")
        sys.exit(1)

if __name__ == "__main__":
    main()
