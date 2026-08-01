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
fn session_meta_uuid() -> Uuid {
    Uuid::parse_str("4C494C4F-434F-5753-0006-000000000000").unwrap()
}
fn audio_info_uuid() -> Uuid {
    Uuid::parse_str("4C494C4F-434F-5753-0007-000000000000").unwrap()
}
fn audio_data_uuid() -> Uuid {
    Uuid::parse_str("4C494C4F-434F-5753-0008-000000000000").unwrap()
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
        4 => "test",
        5 => "removal",
        _ => "general",
    }
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
    /// Signal strength in dBm at scan time (closer to 0 = stronger, e.g.
    /// -50 is a strong signal, -90 is very weak). None if the platform
    /// didn't report one for this advertisement.
    pub rssi: Option<i16>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HeldSession {
    pub id: u32,
    pub name: String,
    pub session_type: String,
    pub count: u32,
    pub ts: u64,
    pub synced: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionMeta {
    pub id: u32,
    pub device_id: String,
    pub name: String,
    pub session_type: u8,
    pub created_at: i64,
    pub tag_count: u32,
    pub synced: bool,
    pub note: String,
    pub has_note_audio: bool,
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
    pub test_result: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub test_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub vaccines: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub notes: Option<String>,
    pub has_audio: bool,
}

// ---------------------------------------------------------------------------
// Internal connection state
// ---------------------------------------------------------------------------

pub struct BleConn {
    pub peripheral: Peripheral,
    pub chars: HashMap<Uuid, Characteristic>,
    /// True if this device exposes AUDIO_INFO/AUDIO_DATA — how old SC01 Plus
    /// handhelds (no recording hardware, no audio characteristics at all)
    /// are told apart from CrowPanel devices at sync time.
    pub supports_audio: bool,
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

pub async fn scan_for_devices() -> Result<(Adapter, Vec<DeviceInfo>), String> {
    let manager = Manager::new().await.map_err(|e| e.to_string())?;
    let adapters = manager.adapters().await.map_err(|e| e.to_string())?;
    eprintln!("[ble] scan: {} adapter(s) found", adapters.len());
    let adapter = adapters
        .into_iter()
        .next()
        .ok_or_else(|| "No Bluetooth adapter found".to_string())?;

    adapter
        .start_scan(ScanFilter::default())
        .await
        .map_err(|e| e.to_string())?;
    eprintln!("[ble] scan: started, waiting 4s...");

    tokio::time::sleep(Duration::from_secs(4)).await;
    adapter.stop_scan().await.ok();

    let peripherals = adapter.peripherals().await.map_err(|e| e.to_string())?;
    eprintln!("[ble] scan: adapter.peripherals() sees {} device(s) total (all names, before filtering)", peripherals.len());
    let mut devices = Vec::new();
    for p in peripherals {
        match p.properties().await {
            Ok(Some(props)) => {
                let name = props.local_name.unwrap_or_default();
                eprintln!("[ble] scan: candidate id={} name=\"{}\" rssi={:?}", p.id(), name, props.rssi);
                if name == "Pilocows" {
                    devices.push(DeviceInfo {
                        id: p.id().to_string(),
                        name,
                        rssi: props.rssi,
                    });
                }
            }
            Ok(None) => eprintln!("[ble] scan: candidate id={} has no properties yet", p.id()),
            Err(e) => eprintln!("[ble] scan: candidate id={} properties() errored: {e}", p.id()),
        }
    }
    Ok((adapter, devices))
}

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------

pub async fn connect(adapter: &Adapter, device_id: &str) -> Result<BleConn, String> {
    let t0 = std::time::Instant::now();
    eprintln!("[ble] connect: looking up device_id={device_id} in adapter.peripherals()...");
    let peripherals = adapter.peripherals().await.map_err(|e| e.to_string())?;
    eprintln!("[ble] connect: adapter.peripherals() returned {} device(s) in {:?}",
              peripherals.len(), t0.elapsed());
    let peripheral = peripherals
        .into_iter()
        .find(|p| p.id().to_string() == device_id)
        .ok_or_else(|| format!("Device {device_id} not found — try scanning again"))?;
    eprintln!("[ble] connect: target peripheral found, checking is_connected()...");
    match peripheral.is_connected().await {
        Ok(c) => eprintln!("[ble] connect: is_connected() -> {c}"),
        Err(e) => eprintln!("[ble] connect: is_connected() errored: {e}"),
    }

    // ESP-Hosted bridges BLE through the ESP32-C6 co-processor over SDIO —
    // an extra hop the connection setup (and the CoreBluetooth-side link
    // establishment) has to clear that a native-BLE handheld wouldn't. This
    // has been observed taking noticeably longer than our deadline even
    // though the handheld's own GAP log shows the link-layer connect
    // completing fine — and observed concretely: after our timeout fired and
    // returned an error, an immediate retry found is_connected() already
    // true and finished in under a second, meaning the OS-level connection
    // actually completed shortly after we gave up on it. So on timeout,
    // check is_connected() before failing instead of abandoning a connection
    // that may have just finished establishing.
    let t1 = std::time::Instant::now();
    eprintln!("[ble] connect: calling peripheral.connect() (45s timeout)...");

    // Log the last-known RSSI every few seconds while the connect attempt is
    // still pending, to help tell "slow because of a weak/interfered signal"
    // apart from other causes. read_rssi() needs an active connection (we
    // don't have one yet), so this reports whatever the OS last cached from
    // the scan — not a live reading, but still useful context on whether the
    // device was ever seen with a strong signal at all.
    // Boxed (not tokio::pin!) so it can be dropped by name below, releasing
    // its borrow of `peripheral` before `peripheral` is moved into BleConn.
    let mut connect_fut = Box::pin(tokio::time::timeout(Duration::from_secs(45), peripheral.connect()));
    let mut rssi_ticker = tokio::time::interval(Duration::from_secs(3));
    rssi_ticker.tick().await; // first tick fires immediately — skip it
    let connect_outcome = loop {
        tokio::select! {
            res = &mut connect_fut => break res,
            _ = rssi_ticker.tick() => {
                match peripheral.properties().await {
                    Ok(Some(props)) => eprintln!(
                        "[ble] connect: still connecting ({:?} elapsed)... last known RSSI: {:?}",
                        t1.elapsed(), props.rssi
                    ),
                    Ok(None) => eprintln!(
                        "[ble] connect: still connecting ({:?} elapsed)... no cached signal info",
                        t1.elapsed()
                    ),
                    Err(e) => eprintln!(
                        "[ble] connect: still connecting ({:?} elapsed)... properties() errored: {e}",
                        t1.elapsed()
                    ),
                }
            }
        }
    };
    drop(connect_fut); // release the borrow of `peripheral` now that we're done with it
    match connect_outcome {
        Ok(Ok(())) => {
            eprintln!("[ble] connect: peripheral.connect() succeeded in {:?}", t1.elapsed());
        }
        Ok(Err(e)) => {
            eprintln!("[ble] connect: peripheral.connect() returned an error after {:?}: {e}", t1.elapsed());
            return Err(e.to_string());
        }
        Err(_) => {
            eprintln!("[ble] connect: peripheral.connect() TIMED OUT after {:?} — checking is_connected() before giving up...", t1.elapsed());
            match peripheral.is_connected().await {
                Ok(true) => {
                    eprintln!("[ble] connect: is_connected() -> true despite timeout, proceeding anyway");
                }
                other => {
                    eprintln!("[ble] connect: is_connected() -> {other:?}, giving up");
                    return Err("Connection timed out — rescan and try again".to_string());
                }
            }
        }
    }

    let t2 = std::time::Instant::now();
    eprintln!("[ble] connect: calling peripheral.discover_services() (15s timeout)...");
    let discover_result = tokio::time::timeout(Duration::from_secs(15), peripheral.discover_services()).await;
    match &discover_result {
        Ok(Ok(())) => eprintln!("[ble] connect: discover_services() succeeded in {:?}", t2.elapsed()),
        Ok(Err(e)) => eprintln!("[ble] connect: discover_services() returned an error after {:?}: {e}", t2.elapsed()),
        Err(_) => eprintln!("[ble] connect: discover_services() TIMED OUT after {:?} (15s limit)", t2.elapsed()),
    }
    discover_result
        .map_err(|_| "Service discovery timed out — the handheld may not be in Sync mode".to_string())?
        .map_err(|e| e.to_string())?;

    let pilocows_svc = svc_uuid();
    let collect_chars = |p: &Peripheral| -> HashMap<Uuid, Characteristic> {
        let mut chars = HashMap::new();
        for c in p.characteristics() {
            if c.service_uuid == pilocows_svc {
                chars.insert(c.uuid, c);
            }
        }
        chars
    };

    let mut chars = collect_chars(&peripheral);
    eprintln!("[ble] connect: found {} Pilocows characteristic(s) on first read ({:?} total)", chars.len(), t0.elapsed());

    // Some backends report discover_services() as complete slightly before
    // their internal characteristic list is fully populated (an async/event
    // timing race, not a real absence) — observed intermittently as "service
    // not found" errors even though the same device connects fine moments
    // later. Give it a couple of short retries before concluding the service
    // genuinely isn't there.
    for attempt in 1..=3 {
        if !chars.is_empty() {
            break;
        }
        eprintln!("[ble] connect: characteristics empty, retry {attempt}/3 after 300ms settle...");
        tokio::time::sleep(Duration::from_millis(300)).await;
        // Re-running discover_services() forces a fresh query rather than
        // relying on whatever the backend already cached internally.
        let _ = peripheral.discover_services().await;
        chars = collect_chars(&peripheral);
        eprintln!("[ble] connect: retry {attempt}/3 -> {} characteristic(s)", chars.len());
    }

    if chars.is_empty() {
        peripheral.disconnect().await.ok();
        return Err("Connected but Pilocows service not found on this device".to_string());
    }

    let supports_audio = chars.contains_key(&audio_info_uuid()) && chars.contains_key(&audio_data_uuid());
    eprintln!("[ble] connect: DONE in {:?} (supports_audio={supports_audio})", t0.elapsed());

    Ok(BleConn { peripheral, chars, supports_audio })
}

// ---------------------------------------------------------------------------
// Read session list
// Handheld sends: [{"id":1,"name":"...","type":1,"status":0,"count":5,"ts":...,"synced":0}]
// ---------------------------------------------------------------------------

pub async fn read_session_list(conn: &BleConn) -> Result<Vec<HeldSession>, String> {
    // Raw shape from handheld — integers for type
    #[derive(Deserialize)]
    struct Raw {
        id: u32,
        name: String,
        #[serde(rename = "type")]
        session_type: u8,
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

    // The very first page has been observed succeeding fast and every
    // subsequent page then vanishing with no trace on the device side at
    // all (no CONTROL write ever logged) — a per-call timeout here, on both
    // the write and the read, pins down exactly which operation is the one
    // that never completes instead of the whole command just eating the
    // outer 30s timeout with zero visibility into where it actually stalled.
    const PAGE_OP_TIMEOUT: Duration = Duration::from_secs(10);

    let mut all: Vec<HeldSession> = Vec::new();
    let mut offset: u32 = 0;
    let mut page_num: u32 = 0;

    loop {
        page_num += 1;
        eprintln!("[ble] list: page {page_num} (offset={offset}) — sending CONTROL list_page...");
        // Tell the handheld which page to return
        tokio::time::timeout(
            PAGE_OP_TIMEOUT,
            send_control(conn, &serde_json::json!({"cmd": "list_page", "offset": offset})),
        )
        .await
        .map_err(|_| format!("list_page CONTROL write timed out ({}s) at offset {offset}", PAGE_OP_TIMEOUT.as_secs()))??;
        eprintln!("[ble] list: page {page_num} — CONTROL write done, sleeping 50ms then reading...");
        tokio::time::sleep(Duration::from_millis(50)).await;

        let raw = tokio::time::timeout(PAGE_OP_TIMEOUT, conn.peripheral.read(char))
            .await
            .map_err(|_| format!("SESSION_LIST read timed out ({}s) at offset {offset}", PAGE_OP_TIMEOUT.as_secs()))?
            .map_err(|e| e.to_string())?;

        let raw_len = raw.len();
        eprintln!("[ble] list: page {page_num} — read done, {raw_len} bytes");

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
        test_result: Option<String>,
        test_name: Option<String>,
        #[serde(default)]
        note: String,
        #[serde(default)]
        has_audio: u8,
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
            test_result: r.test_result,
            test_name: r.test_name,
            vaccines: r.vaccines,
            notes: if r.note.is_empty() { None } else { Some(r.note) },
            has_audio: r.has_audio != 0,
        }));

        if page_len < PAGE_SIZE {
            break;
        }
        offset += PAGE_SIZE as u32;
    }

    Ok(all)
}

// ---------------------------------------------------------------------------
// Read session metadata
// Handheld sends: {"id":1,"device_id":"AA:BB:CC:DD:EE:FF","name":"...","type":1,
//                  "status":0,"created_at":1745000000,"tag_count":5,"synced":0,"note":"..."}
// ---------------------------------------------------------------------------

pub async fn read_session_meta(conn: &BleConn, session_id: u32) -> Result<SessionMeta, String> {
    #[derive(Deserialize)]
    struct Raw {
        id: u32,
        device_id: String,
        name: String,
        #[serde(rename = "type")]
        session_type: u8,
        created_at: i64,
        tag_count: u32,
        synced: u8,
        #[serde(default)]
        note: String,
        #[serde(default)]
        has_note_audio: u8,
    }

    let char = conn
        .chars
        .get(&session_meta_uuid())
        .ok_or("SESSION_META characteristic not found")?;

    // Select the session (also resets data offset on the handheld)
    send_control(conn, &serde_json::json!({"cmd": "select", "id": session_id})).await?;
    tokio::time::sleep(Duration::from_millis(50)).await;

    let raw_bytes = conn
        .peripheral
        .read(char)
        .await
        .map_err(|e| e.to_string())?;

    let raw: Raw = serde_json::from_slice(&raw_bytes).map_err(|e| {
        let preview = String::from_utf8_lossy(&raw_bytes[..raw_bytes.len().min(120)]);
        format!("SESSION_META parse error: {e} (preview: {preview})")
    })?;

    Ok(SessionMeta {
        id: raw.id,
        device_id: raw.device_id,
        name: raw.name,
        session_type: raw.session_type,
        created_at: raw.created_at,
        tag_count: raw.tag_count,
        synced: raw.synced != 0,
        note: raw.note,
        has_note_audio: raw.has_note_audio != 0,
    })
}

// ---------------------------------------------------------------------------
// Read audio (session note or per-tag clip) — old handhelds never have
// supports_audio=true, so these just return Ok(None) for them rather than
// erroring, letting the sync flow proceed with text-only data as before.
// ---------------------------------------------------------------------------

// Must match AUDIO_DATA_PAGE_SIZE in ble_gatt_server.c
const AUDIO_PAGE_SIZE: usize = 480;

#[derive(Deserialize)]
struct AudioInfo {
    exists: bool,
    size: i64,
}

/// Reads whatever audio source is currently selected via CONTROL
/// select_note_audio/select_tag_audio, paging through AUDIO_DATA until a
/// short page (or the reported size) is reached. `on_progress(loaded, total)`
/// is called after every accepted page so callers can drive a UI progress
/// indicator during what can be a 100+ second transfer.
async fn read_selected_audio(
    conn: &BleConn,
    mut on_progress: impl FnMut(usize, usize),
) -> Result<Option<Vec<u8>>, String> {
    let info_char = conn
        .chars
        .get(&audio_info_uuid())
        .ok_or("AUDIO_INFO characteristic not found")?;
    let raw = conn.peripheral.read(info_char).await.map_err(|e| e.to_string())?;
    let info: AudioInfo = serde_json::from_slice(&raw)
        .map_err(|e| format!("AUDIO_INFO parse error: {e}"))?;

    if !info.exists || info.size <= 0 {
        return Ok(None);
    }

    let data_char = conn
        .chars
        .get(&audio_data_uuid())
        .ok_or("AUDIO_DATA characteristic not found")?;

    eprintln!("[ble] audio: starting transfer, {} bytes total", info.size);
    let started = std::time::Instant::now();

    // Each page has been observed taking several hundred ms over this
    // board's ESP-Hosted BLE bridge (extra hop through the companion C6
    // chip). A per-page timeout lets a genuinely stuck link fail fast
    // instead of burning the whole outer command timeout on one page.
    const PAGE_TIMEOUT: Duration = Duration::from_secs(15);

    // Explicit CONTROL "audio_page" write before EVERY read, mirroring
    // SESSION_LIST's paging pattern exactly. An earlier optimization made
    // the device auto-advance its own cursor so this round trip could be
    // skipped — but that required the read callback to mutate shared state
    // on every invocation, which broke under BLE's "Read Long" reassembly
    // (needed here since a 480-byte page exceeds this MTU's single-packet
    // payload): NimBLE re-invokes the SAME read callback multiple times per
    // one logical read here, just like it does for SESSION_LIST, and each
    // extra invocation silently advanced the cursor by another full page
    // the central never saw — losing very close to exactly half of every
    // transfer. Explicitly setting the offset before each read makes the
    // device's read callback idempotent again (repeated invocations return
    // the same, correct page), the same property that already makes
    // SESSION_LIST safe under this exact reassembly behavior.
    const MAX_RETRIES_PER_PAGE: u32 = 5;
    let mut bytes: Vec<u8> = Vec::with_capacity(info.size as usize);
    let mut page_num: u32 = 0;
    let mut retries: u32 = 0;
    loop {
        let offset = bytes.len() as u32;

        send_control(conn, &serde_json::json!({"cmd": "audio_page", "offset": offset})).await?;
        tokio::time::sleep(Duration::from_millis(50)).await;

        let page = tokio::time::timeout(PAGE_TIMEOUT, conn.peripheral.read(data_char))
            .await
            .map_err(|_| format!("audio page read timed out ({}s) at offset {offset}", PAGE_TIMEOUT.as_secs()))?
            .map_err(|e| e.to_string())?;
        let page_len = page.len();

        if page_len == 0 {
            // Genuine end-of-file sentinel from the device.
            break;
        }

        if page_len != AUDIO_PAGE_SIZE {
            retries += 1;
            if retries > MAX_RETRIES_PER_PAGE {
                return Err(format!(
                    "audio page at offset {offset} kept coming back wrong-sized ({page_len}, expected {AUDIO_PAGE_SIZE}) after {MAX_RETRIES_PER_PAGE} retries — link may be dropping data"
                ));
            }
            eprintln!(
                "[ble] audio: page at offset {offset} came back {page_len} bytes (expected {AUDIO_PAGE_SIZE}) — retrying ({retries}/{MAX_RETRIES_PER_PAGE})"
            );
            continue;
        }
        retries = 0;

        bytes.extend_from_slice(&page);
        page_num += 1;
        on_progress(bytes.len().min(info.size as usize), info.size as usize);

        if page_num % 10 == 0 {
            eprintln!(
                "[ble] audio: {}/{} bytes ({:.1}s elapsed)",
                bytes.len().min(info.size as usize),
                info.size,
                started.elapsed().as_secs_f32()
            );
        }
    }

    // Hard integrity check: the "EOF" break above only ever fires on a
    // genuine empty page, but a page silently missing from the middle of
    // the transfer (e.g. the exactly-half-sized undercounts seen in
    // testing) still LOOKS like a normal full page in isolation — nothing
    // about its own length flags it as wrong. The one thing that can't
    // lie is the total: a correctly completed transfer must have
    // accumulated at least info.size bytes before hitting the true EOF
    // sentinel. Catching a short total here turns a silently-corrupted
    // clip into a loud, retryable error instead.
    if bytes.len() < info.size as usize {
        return Err(format!(
            "audio transfer ended early: only {} of {} bytes received before the device signaled end-of-file — link may be dropping data",
            bytes.len(),
            info.size
        ));
    }

    bytes.truncate(info.size as usize);

    eprintln!(
        "[ble] audio: transfer complete, {} bytes in {:.1}s",
        bytes.len(),
        started.elapsed().as_secs_f32()
    );

    Ok(Some(bytes))
}

pub async fn read_note_audio(
    conn: &BleConn,
    session_id: u32,
    on_progress: impl FnMut(usize, usize),
) -> Result<Option<Vec<u8>>, String> {
    if !conn.supports_audio {
        return Ok(None);
    }
    send_control(conn, &serde_json::json!({"cmd": "select", "id": session_id})).await?;
    tokio::time::sleep(Duration::from_millis(50)).await;
    send_control(conn, &serde_json::json!({"cmd": "select_note_audio"})).await?;
    tokio::time::sleep(Duration::from_millis(50)).await;
    read_selected_audio(conn, on_progress).await
}

pub async fn read_tag_audio(
    conn: &BleConn,
    session_id: u32,
    eid: &str,
    on_progress: impl FnMut(usize, usize),
) -> Result<Option<Vec<u8>>, String> {
    if !conn.supports_audio {
        return Ok(None);
    }
    send_control(conn, &serde_json::json!({"cmd": "select", "id": session_id})).await?;
    tokio::time::sleep(Duration::from_millis(50)).await;
    send_control(conn, &serde_json::json!({"cmd": "select_tag_audio", "eid": eid})).await?;
    tokio::time::sleep(Duration::from_millis(50)).await;
    read_selected_audio(conn, on_progress).await
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
