use std::path::{Path, PathBuf};

use serde::Deserialize;

pub const DEFAULT_CONFIG: &str = include_str!("../wonderterm.toml");

#[derive(Deserialize, Clone, Debug, PartialEq, Default)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    pub theme: ThemeMode,
    pub font: Font,
    pub window: Window,
    pub cursor: Cursor,
    pub terminal: Terminal,
}

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Default)]
#[serde(rename_all = "lowercase")]
pub enum ThemeMode {
    /// Follow the WonderlandWM shell's light/dark switch.
    #[default]
    Auto,
    Dark,
    Light,
}

#[derive(Deserialize, Clone, Debug, PartialEq)]
#[serde(default, deny_unknown_fields)]
pub struct Font {
    pub family: String,
    pub size: f32,
    pub line_height: f32,
}

impl Default for Font {
    fn default() -> Self {
        Self {
            family: String::new(),
            size: 13.0,
            line_height: 1.2,
        }
    }
}

#[derive(Deserialize, Clone, Debug, PartialEq)]
#[serde(default, deny_unknown_fields)]
pub struct Window {
    pub opacity: f32,
    pub padding: f32,
    pub columns: usize,
    pub rows: usize,
    pub status_bar: bool,
}

impl Default for Window {
    fn default() -> Self {
        Self {
            opacity: 0.94,
            padding: 10.0,
            columns: 120,
            rows: 30,
            status_bar: true,
        }
    }
}

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Default)]
#[serde(rename_all = "lowercase")]
pub enum Animation {
    /// The trailing edge lags behind the leading one, so the cursor stretches
    /// along its path.
    #[default]
    Smear,
    /// The cursor keeps its shape and slides.
    Glide,
    None,
}

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Default)]
#[serde(rename_all = "lowercase")]
pub enum Vfx {
    None,
    #[default]
    Ripple,
    Sparkle,
}

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Default)]
#[serde(rename_all = "lowercase")]
pub enum Shape {
    #[default]
    Block,
    Beam,
    Underline,
}

#[derive(Deserialize, Clone, Debug, PartialEq)]
#[serde(default, deny_unknown_fields)]
pub struct Cursor {
    pub shape: Shape,
    pub animation: Animation,
    pub duration_ms: f32,
    pub typing_duration_ms: f32,
    pub trail: f32,
    pub vfx: Vfx,
    pub glow: bool,
    pub blink: bool,
    pub blink_interval_ms: f32,
}

impl Default for Cursor {
    fn default() -> Self {
        Self {
            shape: Shape::Block,
            animation: Animation::Smear,
            duration_ms: 150.0,
            typing_duration_ms: 50.0,
            trail: 0.65,
            vfx: Vfx::Ripple,
            glow: true,
            blink: true,
            blink_interval_ms: 600.0,
        }
    }
}

#[derive(Deserialize, Clone, Debug, PartialEq)]
#[serde(default, deny_unknown_fields)]
pub struct Terminal {
    pub shell: String,
    pub args: Vec<String>,
    pub term: String,
    pub scrollback: usize,
    pub scroll_lines: f32,
    /// Ctrl+C and Ctrl+X copy and cut a selection, Ctrl+V pastes.
    pub clipboard_keys: bool,
}

impl Default for Terminal {
    fn default() -> Self {
        Self {
            shell: String::new(),
            args: Vec::new(),
            term: "xterm-256color".into(),
            scrollback: 10_000,
            scroll_lines: 3.0,
            clipboard_keys: true,
        }
    }
}

pub fn default_path() -> PathBuf {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .filter(|p| p.is_absolute())
        .unwrap_or_else(|| home().join(".config"));
    base.join("wonderterm/config.toml")
}

pub fn home() -> PathBuf {
    std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/"))
}

/// A missing file is the defaults; a broken one is an error the caller shows.
pub fn load(path: &Path) -> Result<Config, String> {
    match std::fs::read_to_string(path) {
        Ok(text) => toml::from_str(&text).map_err(|e| format!("{}: {e}", path.display())),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(Config::default()),
        Err(e) => Err(format!("{}: {e}", path.display())),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shipped_config_is_the_defaults() {
        let parsed: Config = toml::from_str(DEFAULT_CONFIG).unwrap();
        assert_eq!(parsed, Config::default());
    }

    #[test]
    fn unknown_keys_are_rejected() {
        assert!(toml::from_str::<Config>("[cursor]\nanimaton = \"smear\"\n").is_err());
    }

    #[test]
    fn partial_tables_keep_the_other_defaults() {
        let parsed: Config = toml::from_str("[cursor]\nanimation = \"glide\"\n").unwrap();
        assert_eq!(parsed.cursor.animation, Animation::Glide);
        assert_eq!(parsed.cursor.duration_ms, Cursor::default().duration_ms);
    }
}
