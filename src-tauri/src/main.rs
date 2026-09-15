#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use sysinfo::System;
use std::process::Command;
use winreg::enums::*;
use winreg::RegKey;

#[derive(Serialize, Deserialize)]
struct UserInfo {
    name: String,
    is_premium: bool,
}

#[tauri::command]
fn get_user_info() -> UserInfo {
    UserInfo {
        name: "Admin".to_string(),
        is_premium: true,
    }
}

#[tauri::command]
fn toggle_adult_filter() {
    println!("Adult filter toggled via Rust backend!");
}

#[tauri::command]
fn kill_debug_apps() {
    println!("Killing taskmgr and debug apps...");
    let mut sys = System::new_all();
    sys.refresh_all();

    let debug_apps = [
        "taskmgr.exe", "resmon.exe", "perfmon.exe",
        "procexp.exe", "procexp64.exe", "procmon.exe",
        "processhacker.exe", "wireshark.exe", "fiddler.exe",
    ];

    for (pid, process) in sys.processes() {
        let lower_name = process.name().to_lowercase();
        for app in debug_apps.iter() {
            if lower_name == *app {
                println!("Killing {} with PID {:?}", lower_name, pid);
                process.kill();
            }
        }
    }
}

#[tauri::command]
fn toggle_internet(enable: bool) {
    println!("Internet block set to: {}", enable);
    if enable {
        let _ = Command::new("netsh")
            .args(["advfirewall", "set", "allprofiles", "firewallpolicy", "blockin,blockout"])
            .status();
    } else {
        let _ = Command::new("netsh")
            .args(["advfirewall", "set", "allprofiles", "firewallpolicy", "blockin,allowout"])
            .status();
    }
}

#[tauri::command]
fn toggle_install(enable: bool) {
    println!("Install block set to: {}", enable);
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    let path = "Software\\Policies\\Microsoft\\Windows\\Installer";

    if enable {
        if let Ok((key, _)) = hkcu.create_subkey(path) {
            let _ = key.set_value("DisableMSI", &2u32);
        }
    } else {
        if let Ok(key) = hkcu.open_subkey_with_flags(path, KEY_WRITE) {
            let _ = key.delete_value("DisableMSI");
        }
    }
}

#[tauri::command]
fn toggle_audio(enable: bool) {
    println!("Ambient Noise set to: {}", enable);
    if enable {
        println!("Starting audio playback (requires rodio crate and assets)...");
    } else {
        println!("Stopping audio playback...");
    }
}

#[tauri::command]
async fn connect_parent() -> Result<String, String> {
    println!("Initiating connection to parent via Firebase...");
    Ok("Connected successfully (Mock)".to_string())
}

#[tauri::command]
fn open_pdf_reader() {
    println!("Opening PDF reader application...");
    let _ = Command::new("cmd")
        .args(["/C", "start", "msedge"])
        .spawn();
}

#[tauri::command]
fn connect_remote() {
    println!("Connecting to remote device...");
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_user_info,
            toggle_adult_filter,
            kill_debug_apps,
            toggle_internet,
            toggle_install,
            toggle_audio,
            connect_parent,
            open_pdf_reader,
            connect_remote
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
