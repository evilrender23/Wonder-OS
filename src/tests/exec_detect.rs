//! `exec-terminal` / `exec-launcher` auto-detection: `$TERMINAL` / `$LAUNCHER`
//! if set, else the first of a preference list installed on `PATH`, else
//! `foot` / `fuzzel`. `exec-file-manager` reads `$FILE_MANAGER`, then asks the
//! desktop's default through `xdg-open`, then walks its own list. The
//! variables are read from the env children get, so the config's `[env]`
//! counts. Every test sets the overrides empty and `PATH` to a private dir, so
//! the process env never leaks in.

use std::collections::HashMap;
use std::os::unix::fs::PermissionsExt;
use std::path::Path;

use super::real::TempDir;
use crate::input::actions::{detect_file_manager, detect_launcher, detect_terminal};

fn install(dir: &Path, name: &str, mode: u32) {
    let path = dir.join(name);
    std::fs::write(&path, "#!/bin/sh\n").unwrap();
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(mode)).unwrap();
}

fn child_env(path: &str) -> HashMap<String, String> {
    HashMap::from([
        ("PATH".to_string(), path.to_string()),
        ("TERMINAL".to_string(), String::new()),
        ("LAUNCHER".to_string(), String::new()),
        ("FILE_MANAGER".to_string(), String::new()),
    ])
}

fn env_for(dir: &TempDir) -> HashMap<String, String> {
    child_env(dir.path().to_str().unwrap())
}

#[test]
fn an_installed_terminal_is_found_on_path() {
    let dir = TempDir::new();
    install(dir.path(), "alacritty", 0o755);
    assert_eq!(detect_terminal(&env_for(&dir)), "alacritty");
}

#[test]
fn the_earlier_preference_wins_when_several_are_installed() {
    let dir = TempDir::new();
    install(dir.path(), "kitty", 0o755);
    install(dir.path(), "alacritty", 0o755);
    assert_eq!(detect_terminal(&env_for(&dir)), "alacritty");
}

#[test]
fn the_distro_terminal_is_preferred_over_the_generic_ones() {
    let dir = TempDir::new();
    install(dir.path(), "foot", 0o755);
    install(dir.path(), "wonderterm", 0o755);
    assert_eq!(detect_terminal(&env_for(&dir)), "wonderterm");
}

#[test]
fn every_path_entry_is_searched() {
    let empty = TempDir::new();
    let bin = TempDir::new();
    install(bin.path(), "kitty", 0o755);
    let path = format!("{}:{}", empty.path().display(), bin.path().display());
    assert_eq!(detect_terminal(&child_env(&path)), "kitty");
}

#[test]
fn only_executable_regular_files_count() {
    let dir = TempDir::new();
    install(dir.path(), "foot", 0o644);
    std::fs::create_dir(dir.path().join("alacritty")).unwrap();
    // One execute bit is enough, as for `execvp`.
    install(dir.path(), "ptyxis", 0o701);
    assert_eq!(detect_terminal(&env_for(&dir)), "ptyxis");
}

#[test]
fn nothing_installed_falls_back_to_foot_and_fuzzel() {
    let dir = TempDir::new();
    assert_eq!(detect_terminal(&env_for(&dir)), "foot");
    assert_eq!(detect_launcher(&env_for(&dir)), "fuzzel");
}

#[test]
fn the_override_in_the_child_env_wins() {
    let dir = TempDir::new();
    install(dir.path(), "alacritty", 0o755);
    let mut env = env_for(&dir);
    env.insert("TERMINAL".into(), "my-term --hold".into());
    env.insert("LAUNCHER".into(), "my-menu".into());
    assert_eq!(detect_terminal(&env), "my-term --hold");
    assert_eq!(detect_launcher(&env), "my-menu");
}

#[test]
fn a_detected_launcher_keeps_its_drun_flags() {
    let dir = TempDir::new();
    install(dir.path(), "rofi", 0o755);
    assert_eq!(detect_launcher(&env_for(&dir)), "rofi -show drun");
}

#[test]
fn the_file_manager_is_the_desktop_default_for_folders() {
    let dir = TempDir::new();
    install(dir.path(), "nautilus", 0o755);
    install(dir.path(), "xdg-open", 0o755);
    assert_eq!(detect_file_manager(&env_for(&dir)), "xdg-open \"$HOME\"");
}

#[test]
fn without_xdg_open_the_first_installed_file_manager_is_used() {
    let dir = TempDir::new();
    install(dir.path(), "thunar", 0o755);
    install(dir.path(), "dolphin", 0o755);
    assert_eq!(detect_file_manager(&env_for(&dir)), "dolphin");
}

#[test]
fn the_file_manager_override_wins_over_the_desktop_default() {
    let dir = TempDir::new();
    install(dir.path(), "xdg-open", 0o755);
    let mut env = env_for(&dir);
    env.insert("FILE_MANAGER".into(), "my-files --new-window".into());
    assert_eq!(detect_file_manager(&env), "my-files --new-window");
}
