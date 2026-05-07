#!/usr/bin/env python3
"""
populate_test_data.py — Seed the Pilocows backend with realistic test data via REST API.

Creates:
  - 1 000 RFID ear tags
  - 1 000 animals (one per tag)
  - 10 health events per animal, randomly distributed across:
    vaccinations / pregnancies / TB tests / weights

Usage:
  python3 populate_test_data.py [--base-url http://127.0.0.1:8742] [--workers 8]
"""

import argparse
import random
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import date, timedelta

import http.client
import threading
import urllib.request
import urllib.error
import json

# ── Config ────────────────────────────────────────────────────────────────────

NUM_TAGS         = 1_000
EVENTS_PER_COW   = 10
BASE_EID         = 276_000_100_000_001   # ISO 11784/85 prefix (Argentina = 276)
DEFAULT_WORKERS  = 4
INTER_ANIMAL_MS  = 20   # ms to sleep between animals within a worker (back-pressure)

# ── Reference data ────────────────────────────────────────────────────────────

BREEDS = [
    "Angus", "Shorthorn", "Hereford", "Holando Argentino",
    "Brangus", "Brahman", "Limangus", "Crossbred",
]

CATEGORIES = ["bull", "cow", "heifer", "steer", "male_calf", "female_calf"]

MALE_NAMES = [
    "Tornillo", "Trueno", "Pampa", "Centauro", "Ciclón",
    "Toro Loco", "Matador", "Potrero", "Pedrón", "Bravío",
    "Fierro", "Rayo", "Cordobés", "Fuerte", "Viejo",
]

FEMALE_NAMES = [
    "Negra", "Manchada", "Blanquita", "Lechera", "Serena",
    "Pampita", "Rosada", "Gordita", "Linda", "Loma",
    "Estrella", "Bonita", "Peregrina", "Zaina", "Clavel",
]

VACCINES = [
    "IBR-BVD", "Foot & Mouth", "Brucellosis", "Anthrax",
    "Neonatal Diarrhea", "Respiratory", "Antiparasitic", "Fly Control",
]

VACCINE_DOSES = ["5 mL", "2 mL", "10 mL", "1 dose", "3 mL"]

PREGNANCY_RESULTS = ["unknown", "not_pregnant", "small_pregnant", "medium_pregnant", "big_pregnant", "rejected"]
TB_RESULTS        = ["negative", "positive", "inconclusive"]

CATEGORY_SEX = {
    "bull":        "male",
    "cow":         "female",
    "heifer":      "female",
    "steer":       "male",
    "male_calf":   "male",
    "female_calf": "female",
}

FEMALE_CATEGORIES = {"cow", "heifer", "female_calf"}


# ── Date helpers ──────────────────────────────────────────────────────────────

TODAY = date.today()

def rand_date(years_back_min: int, years_back_max: int) -> str:
    delta_max = years_back_max * 365
    delta_min = years_back_min * 365
    offset = random.randint(delta_min, delta_max)
    return (TODAY - timedelta(days=offset)).isoformat()

def rand_date_after(from_iso: str, max_days_forward: int = 365) -> str:
    base = date.fromisoformat(from_iso)
    offset = random.randint(1, max_days_forward)
    result = base + timedelta(days=offset)
    if result > TODAY:
        result = TODAY
    return result.isoformat()


# ── HTTP helpers ──────────────────────────────────────────────────────────────

# ── HTTP connection pool (one persistent connection per worker thread) ────────

_local = threading.local()

def _get_conn(host: str, port: int) -> http.client.HTTPConnection:
    """Return the thread-local persistent HTTP connection, reconnecting if needed."""
    conn = getattr(_local, "conn", None)
    if conn is None or conn.host != host or conn.port != port:
        conn = http.client.HTTPConnection(host, port, timeout=30)
        _local.conn = conn
    return conn


def post(base_url: str, path: str, body: dict) -> dict:
    from urllib.parse import urlparse
    parsed = urlparse(base_url)
    host   = parsed.hostname
    port   = parsed.port or 80
    url    = f"/api/v1{path}"
    data   = json.dumps(body).encode()

    # Retry once on BrokenPipeError / stale keep-alive connection
    for attempt in range(2):
        conn = _get_conn(host, port)
        try:
            conn.request("POST", url, body=data,
                         headers={"Content-Type": "application/json",
                                  "Connection": "keep-alive"})
            resp = conn.getresponse()
            raw  = resp.read()
            if resp.status >= 400:
                raise RuntimeError(f"HTTP {resp.status} on POST {url}: {raw.decode()[:200]}")
            return json.loads(raw)
        except (http.client.RemoteDisconnected, BrokenPipeError, ConnectionResetError):
            # Stale connection — close and retry once with a fresh one
            conn.close()
            _local.conn = None
            if attempt == 1:
                raise


def check_server(base_url: str) -> None:
    try:
        urllib.request.urlopen(f"{base_url}/api/v1/tags?limit=1", timeout=5)
    except urllib.error.URLError as e:
        print(f"ERROR: Cannot reach {base_url} — {e}")
        print("Make sure the backend is running before seeding.")
        sys.exit(1)


# ── Seeding helpers ───────────────────────────────────────────────────────────

def create_tag(base_url: str, index: int) -> dict:
    eid = str(BASE_EID + index)
    purchased = rand_date(0, 2)
    return post(base_url, "/tags", {
        "tag_number":   eid,
        "purchased_at": purchased,
        "notes":        "",
    })


def create_animal(base_url: str, tag: dict) -> dict:
    category = random.choice(CATEGORIES)
    sex      = CATEGORY_SEX[category]
    name     = random.choice(FEMALE_NAMES if sex == "female" else MALE_NAMES)
    breed    = random.choice(BREEDS)
    dob      = rand_date(1, 10)   # 1–10 years old

    return post(base_url, "/animals", {
        "tag_id":   tag["id"],
        "name":     name,
        "breed":    breed,
        "category": category,
        "sex":      sex,
        "dob":      dob,
        "notes":    "",
    })


def unique_dates(dob: str, n: int) -> list[str]:
    """Return n distinct ISO dates after dob, sorted ascending."""
    base    = date.fromisoformat(dob)
    max_day = min((TODAY - base).days - 1, 3649)
    if max_day < n:
        # Animal too young — spread from dob+1 day forward
        offsets = list(range(1, n + 1))
    else:
        offsets = random.sample(range(1, max_day + 1), n)
        offsets.sort()
    return [(base + timedelta(days=d)).isoformat() for d in offsets]


def create_vaccination(base_url: str, animal_id: int, event_date: str) -> None:
    vaccine  = random.choice(VACCINES)
    dose     = random.choice(VACCINE_DOSES)
    next_due = rand_date_after(event_date, max_days_forward=365) if random.random() > 0.3 else None
    body: dict = {
        "vaccine":         vaccine,
        "dose":            dose,
        "administered_at": event_date,
        "notes":           "",
    }
    if next_due:
        body["next_due_at"] = next_due
    post(base_url, f"/animals/{animal_id}/vaccinations", body)


def create_pregnancy(base_url: str, animal_id: int, event_date: str) -> None:
    result = random.choices(PREGNANCY_RESULTS, weights=[55, 35, 10])[0]
    body: dict = {
        "result":     result,
        "checked_at": event_date,
        "notes":      "",
    }
    if result in ("small_pregnant", "medium_pregnant", "big_pregnant"):
        body["due_date"] = rand_date_after(event_date, max_days_forward=280)
    post(base_url, f"/animals/{animal_id}/pregnancies", body)


def create_tb_test(base_url: str, animal_id: int, event_date: str) -> None:
    result = random.choices(TB_RESULTS, weights=[88, 5, 7])[0]
    post(base_url, f"/animals/{animal_id}/tb-tests", {
        "result":    result,
        "tested_at": event_date,
        "notes":     "",
    })


def create_weight(base_url: str, animal_id: int, category: str, event_date: str) -> None:
    # Realistic weight ranges per category (kg)
    ranges = {
        "bull":        (500, 900),
        "cow":         (380, 620),
        "heifer":      (250, 430),
        "steer":       (300, 550),
        "male_calf":   (40,  200),
        "female_calf": (35,  180),
    }
    lo, hi = ranges.get(category, (100, 600))
    weight_kg = round(random.uniform(lo, hi), 1)

    post(base_url, f"/animals/{animal_id}/weights", {
        "weight_kg":  weight_kg,
        "weighed_at": event_date,
        "notes":      "",
    })


EVENT_TYPES = ["vaccination", "pregnancy", "tb_test", "weight"]


# ── Worker ────────────────────────────────────────────────────────────────────

def seed_one(base_url: str, index: int, delay_ms: int = INTER_ANIMAL_MS) -> str:
    """Create one tag + one animal + EVENTS_PER_COW events. Returns a status string."""
    tag    = create_tag(base_url, index)
    animal = create_animal(base_url, tag)

    animal_id = animal["id"]
    category  = animal["category"]
    dob       = animal.get("dob") or rand_date(2, 6)
    is_female = category in FEMALE_CATEGORIES

    # Pick event types for all 10 slots up front
    event_weights = [3, 3 if is_female else 0, 2, 2]  # vax / pregnancy / tb / weight
    event_types = random.choices(EVENT_TYPES, weights=event_weights, k=EVENTS_PER_COW)

    # Pre-generate EVENTS_PER_COW unique dates — guarantees no same-day collision
    # per animal across all event tables (each table uses a distinct date column).
    dates = unique_dates(dob, EVENTS_PER_COW)

    for event_type, event_date in zip(event_types, dates):
        if event_type == "vaccination":
            create_vaccination(base_url, animal_id, event_date)
        elif event_type == "pregnancy":
            create_pregnancy(base_url, animal_id, event_date)
        elif event_type == "tb_test":
            create_tb_test(base_url, animal_id, event_date)
        else:
            create_weight(base_url, animal_id, category, event_date)

    if delay_ms > 0:
        time.sleep(delay_ms / 1000)

    return f"  [{index+1:>4}/{NUM_TAGS}] {animal['name']:12s} ({animal['category']:11s}) tag {tag['tag_number']}"


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://127.0.0.1:8742",
                        help="Backend base URL (default: http://127.0.0.1:8742)")
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS,
                        help=f"Parallel HTTP workers (default: {DEFAULT_WORKERS})")
    parser.add_argument("--delay-ms", type=int, default=INTER_ANIMAL_MS,
                        help=f"ms to sleep between animals within each worker (default: {INTER_ANIMAL_MS})")
    args = parser.parse_args()

    check_server(args.base_url)

    print(f"\nPilocows test-data seeder")
    print(f"  Backend  : {args.base_url}")
    print(f"  Tags     : {NUM_TAGS}")
    print(f"  Animals  : {NUM_TAGS}")
    print(f"  Events   : {NUM_TAGS * EVENTS_PER_COW:,} ({EVENTS_PER_COW} per animal)")
    print(f"  Workers  : {args.workers}")
    print(f"  Delay/ms : {args.delay_ms}")
    print()

    t0      = time.monotonic()
    done    = 0
    errors  = 0

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {
            pool.submit(seed_one, args.base_url, i, args.delay_ms): i
            for i in range(NUM_TAGS)
        }
        for fut in as_completed(futures):
            try:
                msg = fut.result()
                done += 1
                print(msg)
            except Exception as exc:
                errors += 1
                idx = futures[fut]
                print(f"  [ERROR] index {idx}: {exc}", file=sys.stderr)

    elapsed = time.monotonic() - t0
    print()
    print(f"Done in {elapsed:.1f}s — {done} animals created, {errors} errors.")
    if errors:
        print("Some records failed. Check that the backend was running and the DB had no unique-key conflicts.")
        sys.exit(1)


if __name__ == "__main__":
    main()
