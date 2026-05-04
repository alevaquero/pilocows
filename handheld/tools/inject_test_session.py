#!/usr/bin/env python3
"""
inject_test_session.py
Inject a 100-record closed weighing session into the handheld SPIFFS.

Flow:
  1. Dump current SPIFFS via esptool.py
  2. Extract files with mkspiffs
  3. Generate session meta + 100 tag records (realistic weights & notes)
  4. Rebuild SPIFFS image
  5. Flash just the data partition
"""

import struct, random, time, os, sys, subprocess, tempfile

# ── Tool / device config ──────────────────────────────────────────────────────
ESPTOOL     = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
MKSPIFFS    = os.path.expanduser(
    "~/.platformio/packages/tool-mkspiffs/mkspiffs_espressif32_espidf"
)
# spiffsgen.py is ESP-IDF's own image builder — it honours CONFIG_SPIFFS_OBJ_NAME_LEN.
# mkspiffs_espressif32_espidf is compiled with OBJ_NAME_LEN=32 but the firmware
# uses CONFIG_SPIFFS_OBJ_NAME_LEN=64, so using mkspiffs to BUILD images produces
# an incompatible layout that triggers format_if_mount_failed on every boot.
# We still use mkspiffs for EXTRACTION (step 2) — it can read short-named files
# from a 64-char image correctly enough to recover existing data.
SPIFFSGEN   = os.path.expanduser(
    "~/.platformio/packages/framework-espidf/components/spiffs/spiffsgen.py"
)
PORT  = "/dev/cu.usbmodem101"
BAUD  = 921600

# ── SPIFFS partition (partitions.csv) ────────────────────────────────────────
SPIFFS_OFFSET    = 0x400000
SPIFFS_SIZE      = 0x400000   # 4 MB
SPIFFS_BLOCK     = 4096
SPIFFS_PAGE      = 256
SPIFFS_OBJ_NAME  = 64         # CONFIG_SPIFFS_OBJ_NAME_LEN — MUST match firmware
SPIFFS_META_LEN  = 4          # CONFIG_SPIFFS_META_LENGTH

# ── session_storage.h constants ───────────────────────────────────────────────
SESSION_NAME_MAX  = 64
SESSION_EID_MAX   = 16
SESSION_DATA_SIZE = 16
SESSION_NOTE_MAX  = 128
SESSION_VAX_MAX   = 15
SESSION_META_SIZE = 132   # sizeof(session_meta_t) — time_t is int64 on ESP-IDF v5
TAG_RECORD_SIZE   = 168   # sizeof(tag_record_t)  — time_t is int64 on ESP-IDF v5

SESSION_TYPE_WEIGHING = 1
SESSION_STATUS_CLOSED = 1

# ID high enough not to interfere with any UI-created sessions
TEST_SESSION_ID = 990

# ── Realistic field notes (most cattle get none) ──────────────────────────────
NOTES_POOL = [
    "", "", "", "", "", "", "",          # ~35 % no note
    "Good body condition",
    "Good body condition",
    "Lame left front — monitor closely",
    "Recently calved, calf at foot",
    "Thin — move to supplement paddock",
    "Dominant animal, keep separate at gate",
    "Ear tag loose, needs replacement",
    "Good growth since last weighing",
    "Below target — increase ration",
    "Sold pending — confirm with Pilo",
    "Quiet temperament, easy to handle",
    "Aggressive at gate — flag for vet",
    "Pregnant — confirm due date",
    "Minor wound on flank, healing well",
    "Back in herd after 7-day isolation",
    "Twin calf — monitor milk supply",
    "Noticeably heavier than last month",
    "Ring worm patch on neck — treating",
    "Slight limp, improve within a week",
    "Eyes clear, coat in good condition",
    "Old injury scar, not affecting movement",
    "Best performer in this mob",
    "First weighing since purchase",
]

# ── Struct helpers ────────────────────────────────────────────────────────────

def pack_session_meta(sid, name, stype, status, created_at, tag_count):
    """Pack a session_meta_t (132 bytes, little-endian, packed)."""
    name_b = name.encode()[:SESSION_NAME_MAX - 1].ljust(SESSION_NAME_MAX, b'\x00')
    blob = (
        struct.pack('<I', sid) +        # id              4
        name_b +                        # name[64]        64
        struct.pack('B', stype) +       # type            1
        struct.pack('B', status) +      # status          1
        struct.pack('<q', created_at) + # created_at      8  (int64_t on ESP-IDF v5)
        struct.pack('<I', tag_count) +  # tag_count       4
        struct.pack('B', 0) +           # vax_count       1
        b'\x00' * SESSION_VAX_MAX +     # vax_ids[15]    15
        struct.pack('B', 0) +           # synced          1
        b'\x00' * 33                    # _pad[33]       33
    )                                   #              = 132
    assert len(blob) == SESSION_META_SIZE
    return blob


def pack_tag_record(eid, scanned_at, weight_kg, note):
    """Pack a tag_record_t (168 bytes, little-endian, packed)."""
    eid_b  = eid.encode()[:SESSION_EID_MAX - 1].ljust(SESSION_EID_MAX, b'\x00')
    data_f = struct.pack('<H', weight_kg) + b'\x00' * (SESSION_DATA_SIZE - 2)
    note_b = note.encode()[:SESSION_NOTE_MAX - 1].ljust(SESSION_NOTE_MAX, b'\x00')
    blob = (
        eid_b +                          # eid[16]       16
        struct.pack('<q', scanned_at) +  # scanned_at     8  (int64_t on ESP-IDF v5)
        data_f +                         # data[16]       16
        note_b                           # note[128]     128
    )                                    #             = 168
    assert len(blob) == TAG_RECORD_SIZE
    return blob

# ── Shell helper ──────────────────────────────────────────────────────────────

def run(cmd, check=True):
    print("  $", " ".join(str(c) for c in cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.stdout.strip(): print(r.stdout.rstrip())
    if r.stderr.strip(): print(r.stderr.rstrip(), file=sys.stderr)
    if check and r.returncode != 0:
        sys.exit(f"\nCommand failed (exit {r.returncode})")
    return r

# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    rng = random.Random(2026_04_23)

    work = tempfile.mkdtemp(prefix="pilocows_spiffs_")
    fs   = os.path.join(work, "fs")
    dump = os.path.join(work, "spiffs_dump.bin")
    img  = os.path.join(work, "spiffs_new.bin")
    os.makedirs(fs, exist_ok=True)

    # ── 1. Dump current SPIFFS ────────────────────────────────────────────────
    print(f"\n[1/5] Reading SPIFFS from device ({SPIFFS_SIZE//1024//1024} MB @ 0x{SPIFFS_OFFSET:06X}) …")
    run(["python3", ESPTOOL,
         "--chip", "esp32s3", "--port", PORT, "--baud", str(BAUD),
         "read_flash", hex(SPIFFS_OFFSET), hex(SPIFFS_SIZE), dump])

    # ── 2. Extract files ──────────────────────────────────────────────────────
    print(f"\n[2/5] Extracting SPIFFS files …")
    run([MKSPIFFS, "-u", fs,
         "-b", str(SPIFFS_BLOCK), "-p", str(SPIFFS_PAGE), "-s", str(SPIFFS_SIZE),
         dump])

    files = sorted(os.listdir(fs))
    print(f"\n       Files currently on device:")
    for f in files:
        sz = os.path.getsize(os.path.join(fs, f))
        print(f"         {f}  ({sz} B)")

    # ── 3. Remove all existing session files ──────────────────────────────────
    print(f"\n       Removing existing session files …")
    for fname in sorted(os.listdir(fs)):
        if fname.startswith("s") and fname.endswith(".bin"):
            path = os.path.join(fs, fname)
            os.remove(path)
            print(f"         deleted {fname}")

    # ── 4. Generate records ───────────────────────────────────────────────────
    print(f"\n[3/5] Generating 100 tag records …")

    # Simulate a 3-hour weighing session ending about 2 hours ago
    end_ts   = int(time.time()) - 7200
    start_ts = end_ts - 10800    # 3 hours of scanning
    # one animal roughly every 108 seconds  (100 animals / 3 h)

    records_bin = b""
    weights = []
    for i in range(1, 101):
        eid        = f"858{i:08d}"           # 11-digit FDX-B EID, country 858 (Venezuela)
        scanned_at = start_ts + (i - 1) * 108 + rng.randint(-15, 15)
        weight_kg  = rng.randint(290, 610)   # realistic Brahman/Criollo range
        note       = rng.choice(NOTES_POOL)
        records_bin += pack_tag_record(eid, scanned_at, weight_kg, note)
        weights.append(weight_kg)

    print(f"       Weight range: {min(weights)}–{max(weights)} kg  "
          f"avg {sum(weights)//len(weights)} kg")
    print(f"       Notes on {sum(1 for n in [rng.choice(NOTES_POOL) for _ in range(100)] if n)} / 100 records")

    rec_file = f"s{TEST_SESSION_ID:04d}.bin"
    with open(os.path.join(fs, rec_file), "wb") as fh:
        fh.write(records_bin)
    print(f"       Wrote {rec_file} ({len(records_bin)} bytes)")

    # ── 5. Build a FRESH sess_idx.bin ─────────────────────────────────────────
    # Never patch an existing index — it may be corrupted from a previous failed
    # run.  Write a clean zeroed buffer with only the test session entry.
    # Existing s00xx.bin record files are left on SPIFFS and are harmless without
    # an index entry (the firmware simply won't list them).
    idx_path = os.path.join(fs, "sess_idx.bin")
    idx_raw  = bytearray(TEST_SESSION_ID * SESSION_META_SIZE)   # all zeros

    meta = pack_session_meta(
        sid        = TEST_SESSION_ID,
        name       = "Test Weighing 100",
        stype      = SESSION_TYPE_WEIGHING,
        status     = SESSION_STATUS_CLOSED,
        created_at = start_ts,
        tag_count  = 100,
    )
    offset = (TEST_SESSION_ID - 1) * SESSION_META_SIZE
    idx_raw[offset:offset + SESSION_META_SIZE] = meta

    with open(idx_path, "wb") as fh:
        fh.write(idx_raw)
    print(f"\n       Wrote fresh sess_idx.bin  ({len(idx_raw)} bytes, session {TEST_SESSION_ID} at offset {offset})")

    # ── 6. Rebuild SPIFFS image ───────────────────────────────────────────────
    # Use spiffsgen.py (ESP-IDF's own tool) instead of mkspiffs.
    # mkspiffs_espressif32_espidf is compiled with SPIFFS_OBJ_NAME_LEN=32, but the
    # firmware uses CONFIG_SPIFFS_OBJ_NAME_LEN=64.  The mismatch makes the firmware
    # SPIFFS library reject the image and reformat the partition on every boot.
    print(f"\n[4/5] Packing new SPIFFS image (spiffsgen.py, obj-name-len={SPIFFS_OBJ_NAME}) …")
    run(["python3", SPIFFSGEN,
         "--page-size",    str(SPIFFS_PAGE),
         "--block-size",   str(SPIFFS_BLOCK),
         "--obj-name-len", str(SPIFFS_OBJ_NAME),
         "--meta-len",     str(SPIFFS_META_LEN),
         "--use-magic",
         "--use-magic-len",
         hex(SPIFFS_SIZE),
         fs,
         img])
    print(f"       {img}  ({os.path.getsize(img):,} bytes)")

    # ── 7. Flash ──────────────────────────────────────────────────────────────
    print(f"\n[5/5] Flashing SPIFFS partition …")
    run(["python3", ESPTOOL,
         "--chip", "esp32s3", "--port", PORT, "--baud", str(BAUD),
         "--before", "default_reset", "--after", "hard_reset",
         "write_flash",
         "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
         "--compress",
         hex(SPIFFS_OFFSET), img])

    print(f"\n✓  Session 'Test Weighing 100' (ID={TEST_SESSION_ID}) injected.")
    print(f"   EIDs: 85800000001 … 85800000100")
    print(f"   Temp files: {work}")

if __name__ == "__main__":
    main()
