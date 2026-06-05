mod ble;
mod commands;

use tauri::Manager;

use btleplug::platform::Adapter;
use tokio::sync::Mutex;

/// Persistent state shared across all Tauri commands.
pub struct AppState {
    /// BLE adapter from the last scan (keeps discovered peripherals alive).
    pub adapter: Mutex<Option<Adapter>>,
    /// Active BLE connection + characteristics map.
    pub conn: Mutex<Option<ble::BleConn>>,
}

impl AppState {
    fn new() -> Self {
        Self {
            adapter: Mutex::new(None),
            conn: Mutex::new(None),
        }
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(AppState::new())
        .setup(|app| {
            let version = app.package_info().version.to_string();
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.set_title(&format!("Pilocows {}", version));
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::ble_scan,
            commands::ble_connect,
            commands::ble_read_sessions,
            commands::ble_read_session_data,
            commands::ble_read_session_meta,
            commands::ble_mark_session_synced,
            commands::ble_delete_session,
            commands::ble_disconnect,
            commands::ble_check_connection,
            commands::save_backup,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
