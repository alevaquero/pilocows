# PlatformIO extra_script (pre-build) — injects FIRMWARE_VERSION from handheld/VERSION
# Usage: add `extra_scripts = pre:version_flag.py` to platformio.ini

Import("env")  # noqa: F821 — injected by PlatformIO

version = open("VERSION").read().strip()
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", f'\\"{version}\\"')])
print(f"[version_flag] FIRMWARE_VERSION = {version}")
