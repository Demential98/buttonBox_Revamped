#!/usr/bin/env python3
import sys, json, pathlib

def main():
    if len(sys.argv) < 2:
        print("Usage: python minify_json.py <input.json> [output.json]")
        sys.exit(1)

    in_file = pathlib.Path(sys.argv[1])
    out_file = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else in_file.with_suffix(".min.json")

    try:
        data = json.load(in_file.open(encoding="utf-8"))
    except Exception as e:
        print(f"Error reading {in_file}: {e}")
        sys.exit(1)

    # Minify: no spaces after commas/colons
    minified = json.dumps(data, separators=(",", ":"))
    out_file.write_text(minified, encoding="utf-8")

    print(f"Minified JSON saved to {out_file} ({len(minified)} bytes)")

if __name__ == "__main__":
    main()
