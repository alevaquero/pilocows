# Pilocows — Architecture Overview

## Components

### 1. Handheld Device (ESP32-S3)
Portable RFID scanner used in the field by the farmer.

**Responsibilities:**
- Read FDX-B RFID ear tags via UART module
- Display tag EID and status on touchscreen
- Store scanned records locally (NVS/SPIFFS)
- Alert via I2S buzzer tone and vibrator motor
- Expose scan list to desktop via BLE GATT

**Does NOT:**
- Connect to internet
- Manage animal records (records are enriched in the desktop app)

### 2. Frontend — Desktop App (Tauri + React)
Cross-platform desktop application (macOS, Windows).

**Responsibilities:**
- Connect to handheld via BLE and pull scan batches
- Manage the full animal record lifecycle (registration, health events, removal)
- Communicate with backend exclusively via REST API
- Launch backend as a sidecar process on startup (Phase 1)

**Does NOT:**
- Contain business logic or data storage directly
- Make assumptions about where the backend runs

### 3. Backend — REST API Server (Rust + Axum + SQLite)
Standalone HTTP server. In Phase 1, bundled as a Tauri sidecar. Designed to run as a remote/cloud server in the future.

**Responsibilities:**
- Persist all animal and health data to SQLite
- Expose a versioned REST API
- Validate and enforce data integrity

**Does NOT:**
- Have any UI logic
- Know about BLE or the handheld
- Depend on Tauri

## Data Flow

```
Field scan session:
  Farmer → [physical RFID tag] → handheld RFID module → ESP32 → local storage

Sync session:
  Farmer → [BLE sync button on handheld] → Desktop BLE central pulls scan list
  Desktop → POST /sync/scans → Backend stores raw scans

Record enrichment:
  Farmer → Desktop UI → selects scan → assigns event type (birth, purchase, etc.)
  Desktop → POST /animals or PATCH /animals/{id} → Backend

Health events:
  Farmer → Desktop UI → enters vaccination / pregnancy / weight data
  Desktop → POST /animals/{id}/vaccinations (etc.) → Backend
```

## Deployment — Phase 1 (Local)

```
macOS / Windows machine
├── Tauri app (frontend)
│   └── spawns backend sidecar on port 8742
└── backend (sidecar, Rust binary)
    └── SQLite file at: {app_data_dir}/pilocows.db
```

## Deployment — Future (Cloud)

```
Farmer's machine
└── Tauri app (frontend only)
    └── configured to point to remote backend URL

Cloud server
└── backend binary
    └── PostgreSQL or managed SQLite (e.g. Turso/libSQL)
```

The frontend only needs a `BACKEND_URL` config change. The backend binary is identical.
