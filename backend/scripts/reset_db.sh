#!/usr/bin/env bash
set -euo pipefail

DB="$(dirname "$0")/../pilocows.db"
DB="$(cd "$(dirname "$DB")" && pwd)/$(basename "$DB")"

# ─────────────────────────────────────────────────────────────────────────────
# ██████████████████████████████████████████████████████████████████████████
# ██                                                                      ██
# ██   !!!  WARNING — ALL DATA WILL BE PERMANENTLY DELETED  !!!           ██
# ██                                                                      ██
# ██   This script wipes every row from every table in pilocows.db:       ██
# ██     • tags            • animals       • scan_events                  ██
# ██     • weights         • vaccinations  • pregnancies                  ██
# ██     • tests        • removals                                     ██
# ██                                                                      ██
# ██   The database schema is kept — only data is removed.               ██
# ██   There is NO undo.  Make a backup first if you need the data.      ██
# ██                                                                      ██
# ██████████████████████████████████████████████████████████████████████████
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo "  ┌─────────────────────────────────────────────────────────────┐"
echo "  │  WARNING: ALL DATA IN pilocows.db WILL BE PERMANENTLY LOST  │"
echo "  └─────────────────────────────────────────────────────────────┘"
echo ""
echo "  Database: $DB"
echo ""
printf "  Type  YES  to confirm: "
read -r answer

if [ "$answer" != "YES" ]; then
    echo ""
    echo "  Aborted — nothing was changed."
    exit 1
fi

echo ""
echo "  Deleting all data…"

sqlite3 "$DB" <<'SQL'
PRAGMA foreign_keys = OFF;
DELETE FROM removals;
DELETE FROM tests;
DELETE FROM pregnancies;
DELETE FROM vaccinations;
DELETE FROM weights;
DELETE FROM scan_events;
DELETE FROM animals;
DELETE FROM tags;
PRAGMA foreign_keys = ON;
SQL

echo "  Done. All tables are empty; schema is intact."
echo ""
