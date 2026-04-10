# Pilocows BLE Protocol

## Roles
- **Handheld (ESP32-S3)**: GATT Server / BLE Peripheral
- **Desktop (Tauri)**: GATT Central

## Service

**Service UUID**: `4C494C4F-434F-5753-0001-000000000000`
Name: `PilocowsScanService`

## Characteristics

### SCAN_LIST
- UUID: `4C494C4F-434F-5753-0002-000000000000`
- Properties: READ, NOTIFY
- Format: JSON array, UTF-8 encoded
- Content:
```json
[
  {
    "eid": "12345678901",
    "scanned_at": "2026-04-09T10:32:00Z",
    "event_type": "weighing",
    "notes": ""
  }
]
```
- Large lists are fragmented using BLE L2CAP or chunked via a length-prefix protocol (TBD based on typical scan count)

### DEVICE_STATUS
- UUID: `4C494C4F-434F-5753-0003-000000000000`
- Properties: READ
- Format: JSON, UTF-8
```json
{
  "scan_count": 42,
  "firmware_version": "0.1.0",
  "language": "es"
}
```

### CONTROL
- UUID: `4C494C4F-434F-5753-0004-000000000000`
- Properties: WRITE
- Format: JSON command
```json
{ "cmd": "clear_list" }
{ "cmd": "set_time", "iso8601": "2026-04-09T10:32:00Z" }
```

## Sync Flow

1. Desktop scans for BLE peripherals advertising `PilocowsScanService`
2. User selects handheld from list and connects
3. Desktop reads `DEVICE_STATUS` → shows scan count to user
4. User confirms sync → Desktop reads `SCAN_LIST`
5. Desktop sends scan batch to backend via `POST /sync/scans`
6. On success, Desktop writes `{"cmd": "clear_list"}` to `CONTROL`
7. Handheld clears its local scan list and confirms via NOTIFY on `DEVICE_STATUS`

## Event Types (handheld selectable before scan)
These are set by the farmer on the handheld screen before scanning:

| Code | ES Label | EN Label |
|------|----------|----------|
| `general` | General | General |
| `weighing` | Pesaje | Weighing |
| `vaccination` | Vacunación | Vaccination |
| `pregnancy_check` | Preñez | Pregnancy Check |
| `tb_test` | Tuberculosis | TB Test |
| `removal` | Baja | Removal |
