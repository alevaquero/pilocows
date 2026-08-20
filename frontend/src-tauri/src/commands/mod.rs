use crate::ble::{self, DeviceInfo, HeldSession, SessionMeta, SessionRecord};
use crate::AppState;
use base64::{engine::general_purpose::STANDARD as BASE64, Engine};
use serde::Serialize;
use tauri::{Emitter, Manager, State};
use tauri_plugin_dialog::DialogExt;
use tokio::time::{timeout, Duration};

/// Emitted to the frontend as the "audio-progress" event while a note/tag
/// clip is downloading — a single transfer can take 100+ seconds over BLE,
/// so the UI needs incremental updates rather than just a final result.
#[derive(Clone, Serialize)]
struct AudioProgressPayload {
    session_id: u32,
    eid: Option<String>, // None for the session note, Some(eid) for a tag clip
    loaded: usize,
    total: usize,
}

// ---------------------------------------------------------------------------
// File save (backup download, CSV report export)
// ---------------------------------------------------------------------------

/// Open the system print dialog for the main window.
#[tauri::command]
pub fn print_window(window: tauri::WebviewWindow) -> Result<(), String> {
    window.print().map_err(|e| e.to_string())
}

/// Save raw bytes straight to the user's Downloads folder, no prompt.
/// Used for the DB backup download, where a fixed, predictable location is
/// preferable to asking every time.
/// Returns the absolute path where the file was written.
#[tauri::command]
pub async fn save_file_to_downloads(
    app: tauri::AppHandle,
    filename: String,
    data: Vec<u8>,
) -> Result<String, String> {
    let download_dir = app.path().download_dir().map_err(|e| e.to_string())?;
    let save_path = download_dir.join(&filename);
    std::fs::write(&save_path, &data).map_err(|e| e.to_string())?;
    Ok(save_path.to_string_lossy().to_string())
}

/// Opens a native "Save As" dialog pre-filled with `filename`, then writes
/// the bytes wherever the user picks. Used for CSV report exports, where —
/// unlike the backup download — the user should get to choose the
/// destination each time.
/// Returns the saved path, or `None` if the user cancelled the dialog.
#[tauri::command]
pub async fn export_csv(
    app: tauri::AppHandle,
    filename: String,
    data: Vec<u8>,
) -> Result<Option<String>, String> {
    let picked = app
        .dialog()
        .file()
        .set_file_name(&filename)
        .add_filter("CSV", &["csv"])
        .blocking_save_file();

    let Some(picked) = picked else {
        return Ok(None); // user cancelled
    };

    let path = picked.into_path().map_err(|e| e.to_string())?;
    std::fs::write(&path, &data).map_err(|e| e.to_string())?;
    Ok(Some(path.to_string_lossy().to_string()))
}

// ---------------------------------------------------------------------------
// BLE scan
// ---------------------------------------------------------------------------

/// Scan for nearby BLE devices (4-second window).
/// Returns all named peripherals found.  The caller filters for "Pilocows".
#[tauri::command]
pub async fn ble_scan(state: State<'_, AppState>) -> Result<Vec<DeviceInfo>, String> {
    let (adapter, devices) = ble::scan_for_devices().await?;
    // Persist the adapter so the discovered peripherals stay alive for connect
    *state.adapter.lock().await = Some(adapter);
    Ok(devices)
}

// ---------------------------------------------------------------------------
// BLE connect
// ---------------------------------------------------------------------------

/// Connect to a peripheral by its id string (as returned by ble_scan).
/// Discovers services and stores the connection in AppState.
#[tauri::command]
pub async fn ble_connect(state: State<'_, AppState>, device_id: String) -> Result<(), String> {
    let adapter_guard = state.adapter.lock().await;
    let adapter = adapter_guard
        .as_ref()
        .ok_or("No scan has been performed yet — call ble_scan first")?;

    // Must exceed ble::connect()'s own internal timeouts (45s connect + 15s
    // discover_services) or this outer wrapper cuts it off first with a
    // less specific error.
    let conn = timeout(Duration::from_secs(75), ble::connect(adapter, &device_id))
        .await
        .map_err(|_| "Connect timed out (75s) — make sure the handheld is in Sync mode".to_string())??;
    drop(adapter_guard);

    *state.conn.lock().await = Some(conn);
    Ok(())
}

// ---------------------------------------------------------------------------
// Read session list
// ---------------------------------------------------------------------------

/// Return the list of sessions stored on the handheld.
///
/// No outer duration cap here on purpose: read_session_list already times
/// out every individual write/read internally (10s each), so a session list
/// with many pages that's still making progress — just slowly, over a weak
/// link — isn't killed by an arbitrary total-time budget. Only a step that
/// itself actually stalls fails, with a specific error naming which one.
#[tauri::command]
pub async fn ble_read_sessions(state: State<'_, AppState>) -> Result<Vec<HeldSession>, String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    ble::read_session_list(conn).await
}

// ---------------------------------------------------------------------------
// Read session data
// ---------------------------------------------------------------------------

/// Read all scan records for a given session from the handheld.
///
/// No outer duration cap — see ble_read_sessions above; read_session_data
/// now times out every individual write/read internally (10s each), so this
/// no longer needs (or should have) a total-time ceiling on top.
#[tauri::command]
pub async fn ble_read_session_data(
    state: State<'_, AppState>,
    session_id: u32,
) -> Result<Vec<SessionRecord>, String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    ble::read_session_data(conn, session_id).await
}

// ---------------------------------------------------------------------------
// Read session metadata
// ---------------------------------------------------------------------------

/// Read the SESSION_META characteristic for the given session id.
#[tauri::command]
pub async fn ble_read_session_meta(
    state: State<'_, AppState>,
    session_id: u32,
) -> Result<SessionMeta, String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    timeout(Duration::from_secs(30), ble::read_session_meta(conn, session_id))
        .await
        .map_err(|_| "Session meta read timed out (30s) — device may be out of range".to_string())?
}

// ---------------------------------------------------------------------------
// Audio capability + reads
// ---------------------------------------------------------------------------

/// Whether the connected device exposes the audio-note characteristics.
/// Old SC01 Plus handhelds don't — the sync flow checks this once up front
/// and skips audio pulls entirely for them, rather than per-clip.
#[tauri::command]
pub async fn ble_supports_audio(state: State<'_, AppState>) -> Result<bool, String> {
    let conn_guard = state.conn.lock().await;
    Ok(conn_guard.as_ref().map(|c| c.supports_audio).unwrap_or(false))
}

/// Read a session's voice note, base64-encoded (ready to drop straight into
/// the backend sync payload's note_audio_b64 field). None if the session has
/// no note audio, or the device doesn't support audio at all.
#[tauri::command]
pub async fn ble_read_session_note_audio(
    state: State<'_, AppState>,
    app: tauri::AppHandle,
    session_id: u32,
) -> Result<Option<String>, String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    // No outer duration cap — see ble_read_sessions above. Audio pages are
    // ~480 bytes each and per-page latency has been observed climbing over
    // the course of a long transfer (likely WiFi/BLE coexistence contention
    // on the C6 co-processor); read_selected_audio's own per-page timeout
    // (15s) plus its retry cap and end-to-end integrity check are what
    // actually guard against a stuck link, so a slow-but-still-progressing
    // transfer — even one that legitimately takes several minutes — isn't
    // killed by an arbitrary total-time budget on top.
    let bytes = ble::read_note_audio(conn, session_id, |loaded, total| {
        let _ = app.emit(
            "audio-progress",
            AudioProgressPayload { session_id, eid: None, loaded, total },
        );
    })
    .await?;
    Ok(bytes.map(|b| BASE64.encode(b)))
}

/// Read a tag's voice note, base64-encoded. See ble_read_session_note_audio.
#[tauri::command]
pub async fn ble_read_tag_audio(
    state: State<'_, AppState>,
    app: tauri::AppHandle,
    session_id: u32,
    eid: String,
) -> Result<Option<String>, String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    // No outer duration cap — see ble_read_session_note_audio above.
    let bytes = ble::read_tag_audio(conn, session_id, &eid, |loaded, total| {
        let _ = app.emit(
            "audio-progress",
            AudioProgressPayload { session_id, eid: Some(eid.clone()), loaded, total },
        );
    })
    .await?;
    Ok(bytes.map(|b| BASE64.encode(b)))
}

// ---------------------------------------------------------------------------
// Mark session synced
// ---------------------------------------------------------------------------

/// Tell the handheld to mark a session as synced.
#[tauri::command]
pub async fn ble_mark_session_synced(
    state: State<'_, AppState>,
    session_id: u32,
) -> Result<(), String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    timeout(Duration::from_secs(15), ble::mark_session_synced(conn, session_id))
        .await
        .map_err(|_| "Mark-synced timed out (15s) — device may be out of range".to_string())?
}

// ---------------------------------------------------------------------------
// Delete session
// ---------------------------------------------------------------------------

/// Tell the handheld to delete a session permanently.
#[tauri::command]
pub async fn ble_delete_session(
    state: State<'_, AppState>,
    session_id: u32,
) -> Result<(), String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    // Single-shot operation — this had no timeout at all before, meaning a
    // stuck write could hang forever with nothing to catch it.
    timeout(Duration::from_secs(15), ble::delete_session(conn, session_id))
        .await
        .map_err(|_| "Delete session timed out (15s) — device may be out of range".to_string())?
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

/// Disconnect from the currently connected device.
#[tauri::command]
pub async fn ble_disconnect(state: State<'_, AppState>) -> Result<(), String> {
    let mut conn_guard = state.conn.lock().await;
    if let Some(conn) = conn_guard.as_ref() {
        ble::disconnect(conn).await?;
    }
    *conn_guard = None;
    Ok(())
}

// ---------------------------------------------------------------------------
// Connection health check
// ---------------------------------------------------------------------------

/// Returns true if the peripheral is still connected at the OS level.
/// Uses try_lock so it never blocks if a sync operation is holding the conn lock.
/// Returns true if the lock is busy (sync in progress — assume still connected).
#[tauri::command]
pub async fn ble_check_connection(state: State<'_, AppState>) -> Result<bool, String> {
    match state.conn.try_lock() {
        Err(_) => Ok(true), // another command holds the lock — assume connected
        Ok(guard) => match guard.as_ref() {
            None => Ok(false),
            Some(conn) => {
                use btleplug::api::Peripheral as _;
                conn.peripheral.is_connected().await.map_err(|e| e.to_string())
            }
        },
    }
}
