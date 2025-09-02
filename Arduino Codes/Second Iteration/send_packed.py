#!/usr/bin/env python3
import sys, json, pathlib, time
try:
    import serial
except Exception:
    print("Install pyserial: pip install pyserial")
    sys.exit(1)

BAUD = 115200

ORDER_KEYS = ['0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F']

def build_compact_arrays(cfg):
    # Build dictionary of unique actions
    bag = []
    idx = {}
    def add(s):
        if s not in idx:
            idx[s] = len(bag)
            bag.append(s)
        return idx[s]

    # K: 4x16 indices (0..N-1) or 255 for none
    K = [[255]*16 for _ in range(4)]
    keys = cfg.get("keys", {})
    for m in ['0','1','2','3']:
        if m in keys:
            for i, label in enumerate(ORDER_KEYS):
                v = keys[m].get(label)
                if isinstance(v, str) and v.strip():
                    K[int(m)][i] = add(v.strip())

    # E: 4x4 indices in order [A+, A-, B+, B-]
    E = [[255]*4 for _ in range(4)]
    enc = cfg.get("encoders", {})
    for m in ['0','1','2','3']:
        if m in enc:
            mE = enc[m]
            for si, key in enumerate(("A+","A-","B+","B-")):
                v = mE.get(key)
                if isinstance(v, str) and v.strip():
                    E[int(m)][si] = add(v.strip())
    return bag, K, E

def pack_binary(dict_list, K, E):
    # header + K + E + strings
    MAGIC = 0x42434647  # 'BCFG'
    ver = 1
    dict_bytes = b""
    for s in dict_list:
        dict_bytes += s.encode('utf-8') + b'\x00'

    import struct
    hdr = struct.pack("<IHH", MAGIC, ver, len(dict_list))
    kbytes = bytes([b for row in K for b in row])
    ebytes = bytes([b for row in E for b in row])
    return hdr + kbytes + ebytes + dict_bytes

def main():
    if len(sys.argv) < 3:
        print("Usage: python send_packed.py <COMPORT> <config.json>")
        sys.exit(1)
    port = sys.argv[1]
    cfg = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))

    dict_list, K, E = build_compact_arrays(cfg)
    blob = pack_binary(dict_list, K, E)
    print(f"Packed size: {len(blob)} bytes, dict={len(dict_list)}")

    with serial.Serial(port, BAUD, timeout=3) as s:
        time.sleep(0.5)
        s.reset_input_buffer()
        # Wait for READY (optional but nice)
        time.sleep(0.3)
        ready = s.read(64).decode(errors='ignore')
        if "READY" not in ready:
            print("No READY banner (continuing anyway)")

        s.write(f"BINCFG {len(blob)}\n".encode())
        time.sleep(0.05)
        s.write(blob)
        resp = s.read(128).decode(errors='ignore').strip()
        print(resp if resp else "No response")

if __name__ == "__main__":
    main()
