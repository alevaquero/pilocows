fn main() {
    // Without these, cargo has no reason to know the binary depends on these
    // files, so editing Info.plist (e.g. adding a usage-description key)
    // silently doesn't trigger a rebuild - the stale binary keeps running and
    // macOS TCC kills it the moment it touches Bluetooth/camera/etc without
    // the description key it doesn't know it now has.
    println!("cargo:rerun-if-changed=Info.plist");
    println!("cargo:rerun-if-changed=entitlements.plist");
    tauri_build::build()
}
