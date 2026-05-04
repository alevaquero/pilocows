use btleplug::api::{
    Central, Characteristic, Manager as _, Peripheral as _, ScanFilter, WriteType,
};
use btleplug::platform::{Adapter, Manager, Peripheral};
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::time::Duration;
use uuid::Uuid;

// ---------------------------------------------------------------------------
// Pilocows BLE UUIDs
// ---------------------------------------------------------------------------

fn svc_uuid() -> Uuid {
    Uuid::parse_str("4C494C4F-434F-5753-0001-000000000000").unwrap()
}
fn session_list_uuid() -> Uuid {
    Uuid::parse_str("4C494C4F-434F-5753-0002-000000000000").unwrap()
}
fn control_uuid() -> Uuid {
    Uuid::parse_str("4C494C4F-434F-5753-0004-000000000000").unwrap()
}
fn session_data_uuid() -> Uuid {
    Uuid::parse_str("4C494C4F-434F-5753-0005-000000000000").unwrap()
}

// ---------------------------------------------------------------------------
// Enum helpers — convert handheld integer values to strings
// ---------------------------------------------------------------------------

// session_type_t: 0=General, 1=Weighing, 2=Vaccination, 3=Pregnancy, 4=TB Test, 5=Removal
fn session_type_str(t: u8) -> &'static str {
    match t {
        1 => "weighing",
        2 => "vaccination",
        3 => "pregnancy",
        4 => "tb_test",
        5 => "removal",
        _ => "general",
    }
}

// session_status_t: 0=open, 1=closed
fn session_status_str(s: u8) -> &'static str {
    if s == 0 { "open" } else { "closed" }
}

fn unix_to_iso(ts: i64) -> String {
    DateTime::<Utc>::from_timestamp(ts, 0)
        .map(|dt| dt.format("%Y-%m-%dT%H:%M:%SZ").to_string())
        .unwrap_or_else(|| ts.to_string())
}

// ---------------------------------------------------------------------------
// Shared types (serialised to TypeScript via Tauri)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceInfo {
    pub id: String,
    pub name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HeldSession {
    pub id: u32,
    pub name: String,
    pub session_type: String,
    pub status: String,
    pub count: u32,
    pub ts: u64,
    pub synced: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionRecord {
    pub eid: String,
    pub event_type: String,
    pub scanned_at: String,
    pub session_id: u32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub weight_kg: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pregnancy_result: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tb_result: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub vaccines: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub notes: Option<String>,
}

// ---------------------------------------------------------------------------
// Internal connection state
// ---------------------------------------------------------------------------

pub struct BleConn {
    pub peripheral: Peripheral,
    pub chars: HashMap<Uuid, Characteristic>,
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

pub async fn scan_for_devices() -> Result<(Adapter, Vec<DeviceInfo>), String> {
    let manager = Manager::new().await.map_err(|e| e.to_string())?;
    let adapters = manager.adapters().await.map_err(|e| e.to_string())?;
    let adapter = adapters
        .into_iter()
        .next()
        .ok_or_else(|| "No Bluetooth adapter found".to_string())?;

    adapter
        .start_scan(ScanFilter::default())
        .await
        .map_err(|e| e.to_string())?;

    tokio::time::sleep(Duration::from_secs(4)).await;
    adapter.stop_scan().await.ok();

    let peripherals = adapter.peripherals().await.map_err(|e| e.to_string())?;
    let mut devices = Vec::new();
    for p in peripherals {
        if let Ok(Some(props)) = p.properties().await {
            let name = props.local_name.unwrap_or_default();
            if name == "Pilocows" {
                devices.push(DeviceInfo {
                    id: p.id().to_string(),
                    name,
                });
            }
        }
    }
    Ok((adapter, devices))
}

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------

pub async fn connect(adapter: &Adapter, device_id: &str) -> Result<BleConn, String> {
    let peripherals = adapter.peripherals().await.map_err(|e| e.to_string())?;
    let peripheral = peripherals
        .into_iter()
        .find(|p| p.id().to_string() == device_id)
        .ok_or_else(|| format!("Device {device_id} not found — try scanning again"))?;

    tokio::time::timeout(Duration::from_secs(12), peripheral.connect())
        .await
        .map_err(|_| "Connection timed out — rescan and try again".to_string())?
        .map_err(|e| e.to_string())?;
    tokio::time::timeout(Duration::from_secs(15), peripheral.discover_services())
        .await
        .map_err(|_| "Service discovery timed out — the handheld may not be in Sync mode".to_string())?
        .map_err(|e| e.to_string())?;

    let pilocows_svc = svc_uuid();
    let mut chars: HashMap<Uuid, Characteristic> = HashMap::new();
    for c in peripheral.characteristics() {
        if c.service_uuid == pilocows_svc {
            chars.insert(c.uuid, c);
        }
    }

    if chars.is_empty() {
        peripheral.disconnect().await.ok();
        return Err("Connected but Pilocows service not found on this device".to_string());
    }

    Ok(BleConn { peripheral, chars })
}

// ---------------------------------------------------------------------------
// Read session list
// Handheld sends: [{"id":1,"name":"...","type":1,"status":0,"count":5,"ts":...,"synced":0}]
// ---------------------------------------------------------------------------

pub async fn read_session_list(conn: &BleConn) -> Result<Vec<HeldSession>, String> {
    // Raw shape from handheld — integers for type/status
    #[derive(Deserialize)]
    struct Raw {
        id: u32,
        name: String,
        #[serde(rename = "type")]
        session_type: u8,
        status: u8,
        count: u32,
        ts: u64,
        synced: u8,
    }

    let char = conn
        .chars
        .get(&session_list_uuid())
        .ok_or("SESSION_LIST characteristic not found")?;

    // Must match SESSION_LIST_PAGE_SIZE in ble_server.cpp
    const PAGE_SIZE: usize = 6;

    let mut all: Vec<HeldSession> = Vec::new();
    let mut offset: u32 = 0;

    loop {
        // Tell the handheld which page to return
        send_control(conn, &serde_json::json!({"cmd": "list_page", "offset": offset})).await?;
        tokio::time::sleep(Duration::from_millis(50)).await;

        let raw = conn
            .peripheral
            .read(char)
            .await
            .map_err(|e| e.to_string())?;

        let raw_len = raw.len();

        let page: Vec<Raw> = serde_json::from_slice(&raw).map_err(|e| {
            let preview = String::from_utf8_lossy(&raw[..raw.len().min(120)]);
            let tail = if raw.len() > 120 {
                format!("…(+{} more bytes)", raw.len() - 120)
            } else {
                String::new()
            };
            format!(
                "SESSION_LIST page {offset} parse error: {e} \
                 (received {raw_len} bytes, preview: {preview}{tail})"
            )
        })?;

        let page_len = page.len();

        all.extend(page.into_iter().map(|r| HeldSession {
            id: r.id,
            name: r.name,
            session_type: session_type_str(r.session_type).to_string(),
            status: session_status_str(r.status).to_string(),
            count: r.count,
            ts: r.ts,
            synced: r.synced != 0,
        }));

        // Fewer items than a full page means this was the last page
        if page_len < PAGE_SIZE {
            break;
        }
        offset += PAGE_SIZE as u32;
    }

    Ok(all)
}

// ---------------------------------------------------------------------------
// Read session data
// Handheld sends: [{"eid":"...","ts":unix,"type":1,"weight_kg":480,"note":"..."}]
// ---------------------------------------------------------------------------

pub async fn read_session_data(
    conn: &BleConn,
    session_id: u32,
) -> Result<Vec<SessionRecord>, String> {
    // Raw shape from handheld
    #[derive(Deserialize)]
    struct Raw {
        eid: String,
        ts: i64,
        #[serde(rename = "type")]
        event_type: u8,
        weight_kg: Option<serde_json::Value>,
        vaccines: Option<String>,
        pregnancy: Option<String>,
        tb_result: Option<String>,
        #[serde(default)]
        note: String,
    }

    let char = conn
        .chars
        .get(&session_data_uuid())
        .ok_or("SESSION_DATA characteristic not found")?;

    // Must match SESSION_DATA_PAGE_SIZE in ble_server.cpp
    const PAGE_SIZE: usize = 5;

    // Select the session first (also resets s_data_offset on the handheld)
    send_control(conn, &serde_json::json!({"cmd": "select", "id": session_id})).await?;
    tokio::time::sleep(Duration::from_millis(50)).await;

    let mut all: Vec<SessionRecord> = Vec::new();
    let mut offset: u32 = 0;

    loop {
        // Set the page window (skip for offset=0 since select already reset it,
        // but send anyway for clarity and safety on subsequent pages)
        if offset > 0 {
            send_control(conn, &serde_json::json!({"cmd": "data_page", "offset": offset})).await?;
            tokio::time::sleep(Duration::from_millis(50)).await;
        }

        let raw = conn
            .peripheral
            .read(char)
            .await
            .map_err(|e| e.to_string())?;

        let raw_len = raw.len();

        let page: Vec<Raw> = serde_json::from_slice(&raw).map_err(|e| {
            let preview = String::from_utf8_lossy(&raw[..raw.len().min(120)]);
            format!(
                "SESSION_DATA page {offset} parse error: {e} \
                 (received {raw_len} bytes, preview: {preview}…)"
            )
        })?;

        let page_len = page.len();

        all.extend(page.into_iter().map(|r| SessionRecord {
            eid: r.eid,
            event_type: session_type_str(r.event_type).to_string(),
            scanned_at: unix_to_iso(r.ts),
            session_id,
            weight_kg: r.weight_kg.and_then(|v| v.as_f64()),
            pregnancy_result: r.pregnancy,
            tb_result: r.tb_result,
            vaccines: r.vaccines,
            notes: if r.note.is_empty() { None } else { Some(r.note) },
        }));

        if page_len < PAGE_SIZE {
            break;
        }
        offset += PAGE_SIZE as u32;
    }

    Ok(all)
}

// ---------------------------------------------------------------------------
// Control commands  (handheld uses "id" key, not "session_id")
// ---------------------------------------------------------------------------

pub async fn mark_session_synced(conn: &BleConn, session_id: u32) -> Result<(), String> {
    send_control(conn, &serde_json::json!({"cmd": "mark_synced", "id": session_id})).await
}

pub async fn delete_session(conn: &BleConn, session_id: u32) -> Result<(), String> {
    send_control(conn, &serde_json::json!({"cmd": "delete", "id": session_id})).await
}

async fn send_control(conn: &BleConn, payload: &serde_json::Value) -> Result<(), String> {
    let char = conn
        .chars
        .get(&control_uuid())
        .ok_or("CONTROL characteristic not found")?;

    let data = serde_json::to_vec(payload).map_err(|e| e.to_string())?;
    conn.peripheral
        .write(char, &data, WriteType::WithResponse)
        .await
        .map_err(|e| e.to_string())
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

pub async fn disconnect(conn: &BleConn) -> Result<(), String> {
    conn.peripheral.disconnect().await.map_err(|e| e.to_string())
}
