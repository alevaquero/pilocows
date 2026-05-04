#!/usr/bin/env python3
"""
Pilocows OTA firmware uploader — pure Python 3 stdlib, no dependencies.

Usage:
  python tools/ota_upload.py <device-ip> <firmware.bin>

Examples:
  python tools/ota_upload.py 192.168.1.42 handheld/.pio/build/sc01plus/firmware.bin
  python tools/ota_upload.py 192.168.1.42 firmware.bin

Tip — equivalent curl one-liner:
  curl -X POST http://<ip>/update \
       -H "Content-Type: application/octet-stream" \
       --data-binary @firmware.bin
"""

import sys
import os
import http.client
import argparse


def upload(host: str, firmware_path: str) -> None:
    size = os.path.getsize(firmware_path)
    print(f"  file : {os.path.basename(firmware_path)}  ({size / 1024:.0f} KB)")
    print(f"  target: http://{host}/update")
    print("  sending...", end="", flush=True)

    with open(firmware_path, "rb") as f:
        data = f.read()

    conn = http.client.HTTPConnection(host, timeout=60)
    try:
        conn.request(
            "POST",
            "/update",
            body=data,
            headers={
                "Content-Type": "application/octet-stream",
                "Content-Length": str(size),
            },
        )
        resp = conn.getresponse()
        body = resp.read().decode("utf-8", errors="replace").strip()
    except OSError as exc:
        print(f"\n✗ Connection error: {exc}")
        sys.exit(1)
    finally:
        conn.close()

    if resp.status == 200:
        print(" OK")
        print("✓ Upload complete — device is rebooting")
    else:
        print(f" FAILED")
        print(f"✗ HTTP {resp.status}: {body}")
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Upload OTA firmware to a Pilocows handheld device.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("host",     help="Device IP address  (e.g. 192.168.1.42)")
    parser.add_argument("firmware", help="Firmware .bin file path")
    args = parser.parse_args()

    if not os.path.isfile(args.firmware):
        print(f"✗ File not found: {args.firmware}")
        sys.exit(1)

    upload(args.host, args.firmware)


if __name__ == "__main__":
    main()
