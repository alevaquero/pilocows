mod ble;
mod commands;

use tauri::Manager;

use btleplug::platform::Adapter;
use tokio::sync::Mutex;

pub struct AppState {
    pub adapter: Mutex<Option<Adapter>>,
    pub conn: Mutex<Option<ble::BleConn>>,
    // Holds the backend process we spawned so we can kill it on exit.
    // Only populated on Windows; always None on other platforms.
    pub backend: std::sync::Mutex<Option<std::process::Child>>,
}

impl AppState {
    fn new() -> Self {
        Self {
            adapter: Mutex::new(None),
            conn: Mutex::new(None),
            backend: std::sync::Mutex::new(None),
        }
    }
}

/// Windows only: try to start the backend sidecar if nothing is already
/// listening on port 8742.  Returns the spawned Child so the caller can
/// store it for cleanup on exit.
#[cfg(target_os = "windows")]
fn start_backend_sidecar() -> Option<std::process::Child> {
    use std::net::TcpListener;

    // If the port is already taken the backend is running — leave it alone.
    if TcpListener::bind("127.0.0.1:8742").is_err() {
        return None;
    }

    let backend_exe = std::env::current_exe()
        .ok()?
        .parent()
        .map(|dir| dir.join("pilocows-backend.exe"))?;

    if !backend_exe.exists() {
        eprintln!("[pilocows] Backend executable not found at {backend_exe:?}");
        return None;
    }

    match std::process::Command::new(&backend_exe).spawn() {
        Ok(child) => {
            eprintln!("[pilocows] Backend started (pid {})", child.id());
            Some(child)
        }
        Err(e) => {
            eprintln!("[pilocows] Failed to start backend: {e}");
            None
        }
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(AppState::new())
        .setup(|app| {
            let version = app.package_info().version.to_string();
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.set_title(&format!("Pilocows {}", version));
            }

            #[cfg(target_os = "windows")]
            {
                let child = start_backend_sidecar();
                if let Ok(mut guard) = app.state::<AppState>().backend.lock() {
                    *guard = child;
                }
            }

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::ble_scan,
            commands::ble_connect,
            commands::ble_read_sessions,
            commands::ble_read_session_data,
            commands::ble_read_session_meta,
            commands::ble_supports_audio,
            commands::ble_read_session_note_audio,
            commands::ble_read_tag_audio,
            commands::ble_mark_session_synced,
            commands::ble_delete_session,
            commands::ble_disconnect,
            commands::ble_check_connection,
            commands::save_file_to_downloads,
            commands::export_csv,
            commands::print_window,
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|app_handle, event| {
            if let tauri::RunEvent::Exit = event {
                // Kill the backend we spawned (Windows only; no-op on other platforms
                // because the backend field will always be None there).
                let child: Option<std::process::Child> = app_handle
                    .state::<AppState>()
                    .backend
                    .lock()
                    .ok()
                    .and_then(|mut g| g.take());
                if let Some(mut child) = child {
                    let _ = child.kill();
                    let _ = child.wait();
                    eprintln!("[pilocows] Backend stopped");
                }
            }
        });
}
