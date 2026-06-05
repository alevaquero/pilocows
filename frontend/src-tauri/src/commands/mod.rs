use crate::ble::{self, DeviceInfo, HeldSession, SessionMeta, SessionRecord};
use crate::AppState;
use tauri::{Manager, State};
use tokio::time::{timeout, Duration};

// ---------------------------------------------------------------------------
// File save (backup download)
// ---------------------------------------------------------------------------

/// Save raw bytes to the user's Downloads folder.
/// Returns the absolute path where the file was written.
#[tauri::command]
pub async fn save_backup(
    app: tauri::AppHandle,
    filename: String,
    data: Vec<u8>,
) -> Result<String, String> {
    let download_dir = app.path().download_dir().map_err(|e| e.to_string())?;
    let save_path = download_dir.join(&filename);
    std::fs::write(&save_path, &data).map_err(|e| e.to_string())?;
    Ok(save_path.to_string_lossy().to_string())
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

    let conn = timeout(Duration::from_secs(30), ble::connect(adapter, &device_id))
        .await
        .map_err(|_| "Connect timed out (30s) — make sure the handheld is in Sync mode".to_string())??;
    drop(adapter_guard);

    *state.conn.lock().await = Some(conn);
    Ok(())
}

// ---------------------------------------------------------------------------
// Read session list
// ---------------------------------------------------------------------------

/// Return the list of sessions stored on the handheld.
#[tauri::command]
pub async fn ble_read_sessions(state: State<'_, AppState>) -> Result<Vec<HeldSession>, String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    timeout(Duration::from_secs(30), ble::read_session_list(conn))
        .await
        .map_err(|_| "Session list read timed out (30s) — device may be out of range".to_string())?
}

// ---------------------------------------------------------------------------
// Read session data
// ---------------------------------------------------------------------------

/// Read all scan records for a given session from the handheld.
#[tauri::command]
pub async fn ble_read_session_data(
    state: State<'_, AppState>,
    session_id: u32,
) -> Result<Vec<SessionRecord>, String> {
    let conn_guard = state.conn.lock().await;
    let conn = conn_guard
        .as_ref()
        .ok_or("Not connected — call ble_connect first")?;
    timeout(Duration::from_secs(60), ble::read_session_data(conn, session_id))
        .await
        .map_err(|_| "Session data read timed out (60s) — device may be out of range".to_string())?
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
    ble::delete_session(conn, session_id).await
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
