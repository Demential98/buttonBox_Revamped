#!/usr/bin/env python3
import sys, json, pathlib

def prune_empty(obj):
    """Recursively drop empty dicts/strings so we don't waste space."""
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            pv = prune_empty(v)
            if pv is None:  # skip empties
                continue
            out[k] = pv
        return out or None
    elif isinstance(obj, list):
        out = [prune_empty(v) for v in obj]
        out = [v for v in out if v is not None]
        return out or None
    elif isinstance(obj, str):
        s = obj.strip()
        return s if s else None
    else:
        return obj

def collect_actions(obj, bag):
    """Collect all unique action strings under keys/encoders leaf nodes."""
    if isinstance(obj, dict):
        for v in obj.values():
            collect_actions(v, bag)
    elif isinstance(obj, list):
        for v in obj:
            collect_actions(v, bag)
    elif isinstance(obj, str):
        bag.add(obj)

def remap_to_indices(obj, index_of):
    """Replace strings with integer indices into dict array."""
    if isinstance(obj, dict):
        return {k: remap_to_indices(v, index_of) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [remap_to_indices(v, index_of) for v in obj]
    elif isinstance(obj, str):
        return index_of[obj]
    else:
        return obj

def main():
    if len(sys.argv) < 2:
        print("Usage: python pack_config.py <input.json> [output.json]")
        sys.exit(1)

    in_path = pathlib.Path(sys.argv[1])
    out_path = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else in_path.with_name("config.packed.json")

    data = json.loads(in_path.read_text(encoding="utf-8"))

    # Keep version, keys, encoders; prune empties
    version = data.get("version", 1)
    keys = prune_empty(data.get("keys", {})) or {}
    enc = prune_empty(data.get("encoders", {})) or {}

    # Build dictionary of unique actions
    bag = set()
    collect_actions(keys, bag)
    collect_actions(enc, bag)
    dict_list = sorted(bag)  # deterministic order
    index_of = {s: i for i, s in enumerate(dict_list)}

    # Replace strings with indices
    keys_idx = remap_to_indices(keys, index_of)
    enc_idx = remap_to_indices(enc, index_of)

    packed = {
        "version": version,
        "dict": dict_list,
        "keys": keys_idx,
        "encoders": enc_idx
    }

    # Minify and write
    out_text = json.dumps(packed, separators=(",", ":"))
    out_path.write_text(out_text, encoding="utf-8")

    # Stats
    raw_bytes = len(json.dumps({"version":version,"keys":keys,"encoders":enc}, separators=(",", ":")).encode("utf-8"))
    packed_bytes = len(out_text.encode("utf-8"))
    print(f"Packed saved to {out_path}")
    print(f"Original(min): {raw_bytes} bytes")
    print(f"Packed(min):   {packed_bytes} bytes")
    print(f"Dictionary entries: {len(dict_list)}")

if __name__ == "__main__":
    main()
