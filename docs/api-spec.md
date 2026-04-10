# Pilocows REST API Specification

Base URL: `http://127.0.0.1:8742/api/v1`

All responses are JSON. All timestamps are ISO 8601 UTC strings.

---

## Tag Inventory

### POST /tags
Register purchased tags (manual or from scan batch).

**Body:**
```json
{
  "tag_number": "12345678901",
  "registration_date": "2026-04-09"
}
```

### GET /tags
List all registered tags.

### GET /tags/{tag_number}
Get a specific tag and its current assignment status.

---

## Animals

### POST /animals
Assign a tag to an animal (registration).

**Body:**
```json
{
  "tag_number": "12345678901",
  "registration_date": "2026-04-09",
  "breed": "Angus",
  "category": "cow",
  "date_of_birth": "2024-01-15",
  "movement_reason": "birth"
}
```

**Breeds:** Angus, Shorthorn, Hereford, Holando Argentino, Brangus, Brahman, Limangus
**Categories:** bull, cow, heifer, steer, male_calf, female_calf
**Movement reasons:** birth, purchase

### GET /animals
List animals. Supports query params: `?category=cow&breed=Angus&active=true`

### GET /animals/{id}
Get full animal profile including all health records.

### PATCH /animals/{id}
Update animal category or other mutable fields.

---

## Vaccinations

### POST /animals/{id}/vaccinations
```json
{
  "procedure_date": "2026-04-09",
  "vaccine_type": "foot_and_mouth",
  "description": "Annual dose",
  "supplier": "Biogénesis Bagó",
  "dose_ml": 2.0
}
```

**Vaccine types:** foot_and_mouth, brucellosis, anthrax, neonatal_diarrhea, antiparasitic, respiratory, fly_control

### GET /animals/{id}/vaccinations

---

## Pregnancy

### POST /animals/{id}/pregnancies
```json
{
  "procedure_date": "2026-04-09",
  "result": "mid_pregnancy",
  "action_if_empty": null
}
```

**Results:** advanced_pregnancy, mid_pregnancy, early_pregnancy, empty
**Action if empty:** cull, last_chance, remain_in_herd

### GET /animals/{id}/pregnancies

---

## TB Tests

### POST /animals/{id}/tb-tests
```json
{
  "date": "2026-04-09",
  "result": "negative"
}
```

### GET /animals/{id}/tb-tests

---

## Weights

### POST /animals/{id}/weights
```json
{
  "date": "2026-04-09",
  "weight_kg": 320.5
}
```

### GET /animals/{id}/weights

---

## Removal from Herd

### POST /animals/{id}/removal
```json
{
  "date": "2026-04-09",
  "reason": "sale"
}
```

**Reasons:** death, sale

---

## Scan Sync

### POST /sync/scans
Ingest a batch of raw scans from the handheld (sent by the desktop after BLE pull).

**Body:**
```json
{
  "device_id": "pilocows-handheld-001",
  "scans": [
    {
      "eid": "12345678901",
      "scanned_at": "2026-04-09T10:32:00Z",
      "event_type": "weighing",
      "notes": ""
    }
  ]
}
```

Returns list of EIDs that are unregistered (not yet in tag inventory), so the desktop can prompt the farmer.
