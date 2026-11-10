//! A window's app icon for the SSD title bar: `app_id` → icon name (the
//! desktop entry's `Icon=`, else the id itself) → file in the freedesktop icon
//! theme → premultiplied RGBA at the bar's pixel size.
//!
//! Lookups touch the filesystem (theme indices, desktop entries, decoding), so
//! they run on one background thread. The render pass polls [`lookup`] every
//! frame: the first call for an `(app_id, size)` queues it and reports
//! [`AppIcon::Pending`]; the worker publishes the result — negative results
//! too, so each pair is resolved once per process — and fires the callback
//! registered with [`set_on_ready`] so the bar redraws.

use std::collections::{HashMap, HashSet};
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{Receiver, Sender};
use std::sync::{Arc, Mutex};

use crate::desktop_entry::DesktopEntryCache;
use crate::sync::MutexExt;

/// Generic theme icon used when an app has none of its own.
pub const FALLBACK_ICON: &str = "application-x-executable";

/// A square, premultiplied RGBA image, `size` × `size` pixels.
#[derive(Debug, PartialEq, Eq)]
pub struct IconImage {
    pub size: u32,
    pub rgba: Vec<u8>,
}

/// The state of a title-bar icon lookup.
#[derive(Debug, Clone)]
pub enum AppIcon {
    /// Queued on the worker; the bar leaves the slot empty rather than flash
    /// the fallback mark for a frame.
    Pending,
    /// Neither the app nor the generic fallback icon exists.
    Missing,
    Image(Arc<IconImage>),
}

impl PartialEq for AppIcon {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (AppIcon::Pending, AppIcon::Pending) | (AppIcon::Missing, AppIcon::Missing) => true,
            (AppIcon::Image(a), AppIcon::Image(b)) => Arc::ptr_eq(a, b),
            _ => false,
        }
    }
}

type Key = (String, u32);

struct Shared {
    results: HashMap<Key, AppIcon>,
    requests: Option<Sender<Key>>,
}

static SHARED: Mutex<Option<Shared>> = Mutex::new(None);
#[allow(clippy::type_complexity)]
static ON_READY: Mutex<Option<Box<dyn Fn() + Send>>> = Mutex::new(None);

/// Run `notify` (on the worker thread) whenever a lookup completes. Replaces
/// any previously registered callback.
pub fn set_on_ready(notify: impl Fn() + Send + 'static) {
    *ON_READY.lock_or_recover() = Some(Box::new(notify));
}

/// Non-blocking: the icon for `app_id` at `size` px, queuing the lookup on
/// first use.
pub fn lookup(app_id: &str, size: u32) -> AppIcon {
    let key = (app_id.to_string(), size);
    let mut guard = SHARED.lock_or_recover();
    let shared = guard.get_or_insert_with(|| Shared {
        results: HashMap::new(),
        requests: spawn_worker(),
    });
    if let Some(icon) = shared.results.get(&key) {
        return icon.clone();
    }
    let queued = shared
        .requests
        .as_ref()
        .is_some_and(|tx| tx.send(key.clone()).is_ok());
    let state = if queued {
        AppIcon::Pending
    } else {
        AppIcon::Missing
    };
    shared.results.insert(key, state.clone());
    state
}

fn spawn_worker() -> Option<Sender<Key>> {
    let (tx, rx) = std::sync::mpsc::channel();
    let spawned = std::thread::Builder::new()
        .name("driftwm-app-icons".into())
        .spawn(move || worker(rx));
    match spawned {
        Ok(_) => Some(tx),
        Err(err) => {
            tracing::warn!("failed to spawn app-icon thread: {err}");
            None
        }
    }
}

fn worker(rx: Receiver<Key>) {
    let mut entries = DesktopEntryCache::from_env();
    let themes = IconThemes::from_env();
    while let Ok((app_id, size)) = rx.recv() {
        entries.refresh();
        let icon = load_app_icon(&app_id, Some(&entries), &themes, size)
            .map_or(AppIcon::Missing, |img| AppIcon::Image(Arc::new(img)));
        if let Some(shared) = SHARED.lock_or_recover().as_mut() {
            shared.results.insert((app_id, size), icon);
        }
        if let Some(notify) = ON_READY.lock_or_recover().as_ref() {
            notify();
        }
    }
}

/// Resolve and rasterise `app_id`'s icon, falling back to [`FALLBACK_ICON`].
pub fn load_app_icon(
    app_id: &str,
    entries: Option<&DesktopEntryCache>,
    themes: &IconThemes,
    size: u32,
) -> Option<IconImage> {
    icon_names(app_id, entries)
        .into_iter()
        .chain(std::iter::once(FALLBACK_ICON.to_string()))
        .find_map(|name| {
            let path = themes.find(&name, size)?;
            rasterize(&path, size)
        })
}

/// Icon names to try for `app_id`, best first.
fn icon_names(app_id: &str, entries: Option<&DesktopEntryCache>) -> Vec<String> {
    let mut names = Vec::new();
    if let Some(icon) = entries.and_then(|e| e.icon_name(app_id)) {
        names.push(icon);
    }
    if !app_id.is_empty() {
        names.push(app_id.to_string());
        names.push(app_id.to_lowercase());
        if let Some((_, last)) = app_id.rsplit_once('.') {
            names.push(last.to_lowercase());
        }
    }
    let mut seen = HashSet::new();
    names.retain(|n| !n.is_empty() && seen.insert(n.clone()));
    names
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DirKind {
    Fixed,
    Scalable,
    Threshold,
}

#[derive(Debug, Clone)]
struct ThemeDir {
    subdir: String,
    size: u32,
    scale: u32,
    min: u32,
    max: u32,
    threshold: u32,
    kind: DirKind,
    apps: bool,
}

impl ThemeDir {
    /// How far this directory's icons are from `px`; upscaling costs more
    /// than downscaling, which stays sharp.
    fn distance(&self, px: u32) -> u32 {
        let (lo, hi) = match self.kind {
            DirKind::Fixed => (self.size, self.size),
            DirKind::Scalable => (self.min, self.max),
            DirKind::Threshold => (
                self.size.saturating_sub(self.threshold),
                self.size + self.threshold,
            ),
        };
        let (lo, hi) = (lo * self.scale, hi * self.scale);
        if px < lo {
            lo - px
        } else if px > hi {
            (px - hi) * 8
        } else {
            0
        }
    }
}

#[derive(Debug)]
struct Theme {
    roots: Vec<PathBuf>,
    dirs: Vec<ThemeDir>,
}

/// An icon theme and its inheritance chain, ending in `hicolor`, plus the
/// pixmap directories searched last.
#[derive(Debug)]
pub struct IconThemes {
    chain: Vec<Theme>,
    pixmaps: Vec<PathBuf>,
}

impl IconThemes {
    /// Load `theme` and everything it inherits from the `base_dirs` icon roots
    /// (precedence order).
    pub fn new(theme: &str, base_dirs: &[PathBuf], pixmaps: Vec<PathBuf>) -> Self {
        let mut chain = Vec::new();
        let mut seen = HashSet::new();
        let mut stack = vec![theme.to_string()];
        while let Some(name) = stack.pop() {
            if name == "hicolor" || !seen.insert(name.clone()) {
                continue;
            }
            if let Some((loaded, inherits)) = load_theme(&name, base_dirs) {
                chain.push(loaded);
                stack.extend(inherits.into_iter().rev());
            }
        }
        if let Some((hicolor, _)) = load_theme("hicolor", base_dirs) {
            chain.push(hicolor);
        }
        Self { chain, pixmaps }
    }

    /// The user's theme (`QS_ICON_THEME`, else KDE's `[Icons] Theme=`, else
    /// `hicolor`) over the XDG icon roots.
    pub fn from_env() -> Self {
        let theme = std::env::var("QS_ICON_THEME")
            .ok()
            .filter(|t| !t.is_empty())
            .or_else(kde_icon_theme)
            .unwrap_or_else(|| "hicolor".to_string());
        let (icons, pixmaps) = default_icon_dirs();
        Self::new(&theme, &icons, pixmaps)
    }

    /// Path of the best file for icon `name` at `px` pixels. Absolute names
    /// (an `Icon=` given as a path) are used as-is.
    pub fn find(&self, name: &str, px: u32) -> Option<PathBuf> {
        let as_path = Path::new(name);
        if as_path.is_absolute() {
            return as_path.is_file().then(|| as_path.to_path_buf());
        }
        if name.contains('/') {
            return None;
        }
        self.chain
            .iter()
            .find_map(|theme| find_in_theme(theme, name, px))
            .or_else(|| {
                self.pixmaps.iter().find_map(|dir| {
                    ["png", "svg"]
                        .iter()
                        .map(|ext| dir.join(format!("{name}.{ext}")))
                        .find(|p| p.is_file())
                })
            })
    }
}

fn find_in_theme(theme: &Theme, name: &str, px: u32) -> Option<PathBuf> {
    let mut best: Option<((u32, u8, u8), PathBuf)> = None;
    for dir in &theme.dirs {
        for root in &theme.roots {
            for (ext, kind_rank) in [("png", 0u8), ("svg", 1u8)] {
                let path = root.join(&dir.subdir).join(format!("{name}.{ext}"));
                if !path.is_file() {
                    continue;
                }
                // A vector file scales to any size without loss, so it ties
                // a perfect bitmap; an exact PNG still wins the tie.
                let distance = if ext == "svg" { 0 } else { dir.distance(px) };
                let score = (distance, kind_rank, u8::from(!dir.apps));
                if best.as_ref().is_none_or(|(b, _)| score < *b) {
                    best = Some((score, path));
                }
            }
        }
    }
    best.map(|(_, path)| path)
}

fn load_theme(name: &str, base_dirs: &[PathBuf]) -> Option<(Theme, Vec<String>)> {
    let roots: Vec<PathBuf> = base_dirs
        .iter()
        .map(|b| b.join(name))
        .filter(|p| p.is_dir())
        .collect();
    let index = roots
        .iter()
        .find_map(|r| std::fs::read_to_string(r.join("index.theme")).ok())?;
    let (dirs, inherits) = parse_index(&index);
    Some((Theme { roots, dirs }, inherits))
}

fn parse_index(index: &str) -> (Vec<ThemeDir>, Vec<String>) {
    let mut sections: HashMap<String, HashMap<String, String>> = HashMap::new();
    let mut current = String::new();
    for line in index.lines().map(str::trim) {
        if let Some(section) = line.strip_prefix('[').and_then(|l| l.strip_suffix(']')) {
            current = section.to_string();
        } else if let Some((k, v)) = line.split_once('=') {
            sections
                .entry(current.clone())
                .or_default()
                .insert(k.trim().to_string(), v.trim().to_string());
        }
    }
    let list = |v: Option<&String>| -> Vec<String> {
        v.map(|s| {
            s.split(',')
                .map(str::trim)
                .filter(|s| !s.is_empty())
                .map(str::to_string)
                .collect()
        })
        .unwrap_or_default()
    };
    let head = sections.get("Icon Theme");
    let inherits = list(head.and_then(|h| h.get("Inherits")));
    let mut subdirs = list(head.and_then(|h| h.get("Directories")));
    subdirs.extend(list(head.and_then(|h| h.get("ScaledDirectories"))));

    let dirs = subdirs
        .into_iter()
        .filter_map(|subdir| {
            let keys = sections.get(&subdir)?;
            let num = |k: &str| keys.get(k).and_then(|v| v.parse::<u32>().ok());
            let size = num("Size")?;
            let kind = match keys.get("Type").map(String::as_str) {
                Some("Fixed") => DirKind::Fixed,
                Some("Scalable") => DirKind::Scalable,
                _ => DirKind::Threshold,
            };
            Some(ThemeDir {
                size,
                scale: num("Scale").unwrap_or(1).max(1),
                min: num("MinSize").unwrap_or(size),
                max: num("MaxSize").unwrap_or(size),
                threshold: num("Threshold").unwrap_or(2),
                kind,
                apps: keys
                    .get("Context")
                    .is_some_and(|c| c == "Applications" || c == "Apps"),
                subdir,
            })
        })
        .collect();
    (dirs, inherits)
}

fn kde_icon_theme() -> Option<String> {
    let config_home = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .filter(|p| p.is_absolute())
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    let text = std::fs::read_to_string(config_home.join("kdeglobals")).ok()?;
    let mut in_icons = false;
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') {
            in_icons = line == "[Icons]";
        } else if in_icons && let Some(theme) = line.strip_prefix("Theme=") {
            let theme = theme.trim();
            if !theme.is_empty() {
                return Some(theme.to_string());
            }
        }
    }
    None
}

/// Icon roots (`$XDG_DATA_HOME/icons`, `~/.icons`, `$XDG_DATA_DIRS/icons`) and
/// pixmap directories.
fn default_icon_dirs() -> (Vec<PathBuf>, Vec<PathBuf>) {
    let home = std::env::var_os("HOME").map(PathBuf::from);
    let mut icons = Vec::new();
    let data_home = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .filter(|p| p.is_absolute())
        .or_else(|| home.as_ref().map(|h| h.join(".local/share")));
    if let Some(dir) = data_home {
        icons.push(dir.join("icons"));
    }
    if let Some(h) = &home {
        icons.push(h.join(".icons"));
    }
    let data_dirs = std::env::var_os("XDG_DATA_DIRS")
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| OsString::from("/usr/local/share:/usr/share"));
    let bases: Vec<PathBuf> = std::env::split_paths(&data_dirs)
        .filter(|p| p.is_absolute())
        .collect();
    icons.extend(bases.iter().map(|b| b.join("icons")));
    let pixmaps = bases.iter().map(|b| b.join("pixmaps")).collect();
    (icons, pixmaps)
}

/// Far past any real icon file.
const MAX_ICON_BYTES: u64 = 8 << 20;

/// Decode `path` (PNG/JPEG/SVG) into a `size`² premultiplied RGBA image, the
/// artwork fitted and centred.
pub fn rasterize(path: &Path, size: u32) -> Option<IconImage> {
    if size == 0 || size > 1024 {
        return None;
    }
    let meta = std::fs::metadata(path).ok()?;
    if !meta.is_file() || meta.len() > MAX_ICON_BYTES {
        return None;
    }
    let data = std::fs::read(path).ok()?;
    let is_svg = path
        .extension()
        .and_then(|e| e.to_str())
        .is_some_and(|e| e.eq_ignore_ascii_case("svg") || e.eq_ignore_ascii_case("svgz"));
    if is_svg {
        rasterize_svg(&data, size)
    } else {
        rasterize_bitmap(&data, size)
    }
}

fn rasterize_svg(data: &[u8], size: u32) -> Option<IconImage> {
    let tree = resvg::usvg::Tree::from_data(data, &resvg::usvg::Options::default()).ok()?;
    let mut pixmap = resvg::tiny_skia::Pixmap::new(size, size)?;
    let svg = tree.size();
    let fit = size as f32 / svg.width().max(svg.height());
    let (dx, dy) = (
        (size as f32 - svg.width() * fit) / 2.0,
        (size as f32 - svg.height() * fit) / 2.0,
    );
    let transform = resvg::tiny_skia::Transform::from_row(fit, 0.0, 0.0, fit, dx, dy);
    resvg::render(&tree, transform, &mut pixmap.as_mut());
    Some(IconImage {
        size,
        rgba: pixmap.take(),
    })
}

fn rasterize_bitmap(data: &[u8], size: u32) -> Option<IconImage> {
    let mut img = image::load_from_memory(data).ok()?.into_rgba8();
    // Premultiply before filtering so transparent pixels don't bleed colour.
    for px in img.pixels_mut() {
        let a = px[3] as u32;
        for c in &mut px.0[..3] {
            *c = ((*c as u32 * a + 127) / 255) as u8;
        }
    }
    let (w, h) = img.dimensions();
    let fit = size as f64 / w.max(h).max(1) as f64;
    let (fw, fh) = (
        ((w as f64 * fit).round() as u32).clamp(1, size),
        ((h as f64 * fit).round() as u32).clamp(1, size),
    );
    let scaled = if (fw, fh) == (w, h) {
        img
    } else {
        image::imageops::resize(&img, fw, fh, image::imageops::FilterType::Lanczos3)
    };
    let mut out = image::RgbaImage::new(size, size);
    image::imageops::overlay(
        &mut out,
        &scaled,
        ((size - fw) / 2) as i64,
        ((size - fh) / 2) as i64,
    );
    Some(IconImage {
        size,
        rgba: out.into_raw(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    struct TempDir {
        path: PathBuf,
    }

    impl TempDir {
        fn new() -> Self {
            use std::sync::atomic::{AtomicUsize, Ordering};
            static COUNTER: AtomicUsize = AtomicUsize::new(0);
            let n = COUNTER.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir()
                .join(format!("driftwm-app-icon-test-{}-{n}", std::process::id()));
            std::fs::create_dir_all(&path).unwrap();
            Self { path }
        }

        fn write(&self, rel: &str, contents: &[u8]) -> PathBuf {
            let path = self.path.join(rel);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::File::create(&path)
                .unwrap()
                .write_all(contents)
                .unwrap();
            path
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.path);
        }
    }

    fn png(side: u32, rgba: [u8; 4]) -> Vec<u8> {
        let img = image::RgbaImage::from_pixel(side, side, image::Rgba(rgba));
        let mut out = std::io::Cursor::new(Vec::new());
        img.write_to(&mut out, image::ImageFormat::Png).unwrap();
        out.into_inner()
    }

    const RED_SVG: &[u8] = br##"<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#ff0000"/></svg>"##;

    fn index(name: &str, inherits: &str, dirs: &[(&str, &str)]) -> String {
        let list: Vec<&str> = dirs.iter().map(|(d, _)| *d).collect();
        let mut s = format!(
            "[Icon Theme]\nName={name}\nInherits={inherits}\nDirectories={}\n",
            list.join(",")
        );
        for (dir, body) in dirs {
            s.push_str(&format!("\n[{dir}]\n{body}\n"));
        }
        s
    }

    /// A user theme inheriting a parent theme, plus hicolor.
    fn fixture() -> (TempDir, IconThemes) {
        let tmp = TempDir::new();
        tmp.write(
            "icons/Child/index.theme",
            index(
                "Child",
                "Parent",
                &[(
                    "apps/scalable",
                    "Size=64\nMinSize=16\nMaxSize=512\nType=Scalable\nContext=Applications",
                )],
            )
            .as_bytes(),
        );
        tmp.write("icons/Child/apps/scalable/vector.svg", RED_SVG);
        tmp.write(
            "icons/Parent/index.theme",
            index(
                "Parent",
                "",
                &[
                    ("16x16/apps", "Size=16\nType=Fixed\nContext=Applications"),
                    ("48x48/apps", "Size=48\nType=Fixed\nContext=Applications"),
                ],
            )
            .as_bytes(),
        );
        tmp.write(
            "icons/Parent/16x16/apps/bitmap.png",
            &png(16, [0, 0, 255, 255]),
        );
        tmp.write(
            "icons/Parent/48x48/apps/bitmap.png",
            &png(48, [0, 255, 0, 255]),
        );
        tmp.write(
            "icons/hicolor/index.theme",
            index(
                "Hicolor",
                "",
                &[("32x32/apps", "Size=32\nType=Fixed\nContext=Applications")],
            )
            .as_bytes(),
        );
        tmp.write(
            "icons/hicolor/32x32/apps/application-x-executable.png",
            &png(32, [9, 9, 9, 255]),
        );
        tmp.write("pixmaps/legacy.png", &png(8, [1, 2, 3, 255]));
        let themes = IconThemes::new(
            "Child",
            &[tmp.path.join("icons")],
            vec![tmp.path.join("pixmaps")],
        );
        (tmp, themes)
    }

    #[test]
    fn finds_an_icon_in_the_theme_itself() {
        let (tmp, themes) = fixture();
        assert_eq!(
            themes.find("vector", 22),
            Some(tmp.path.join("icons/Child/apps/scalable/vector.svg"))
        );
    }

    #[test]
    fn follows_inherits_and_picks_the_closest_size_at_or_above() {
        let (tmp, themes) = fixture();
        assert_eq!(
            themes.find("bitmap", 22),
            Some(tmp.path.join("icons/Parent/48x48/apps/bitmap.png"))
        );
        assert_eq!(
            themes.find("bitmap", 16),
            Some(tmp.path.join("icons/Parent/16x16/apps/bitmap.png"))
        );
    }

    #[test]
    fn falls_back_to_hicolor_then_pixmaps() {
        let (tmp, themes) = fixture();
        assert_eq!(
            themes.find("application-x-executable", 22),
            Some(
                tmp.path
                    .join("icons/hicolor/32x32/apps/application-x-executable.png")
            )
        );
        assert_eq!(
            themes.find("legacy", 22),
            Some(tmp.path.join("pixmaps/legacy.png"))
        );
        assert_eq!(themes.find("nothing", 22), None);
    }

    #[test]
    fn rasterizes_svg_and_png_to_the_requested_size() {
        let (tmp, themes) = fixture();
        let svg = rasterize(&themes.find("vector", 22).unwrap(), 22).unwrap();
        assert_eq!(svg.size, 22);
        assert_eq!(svg.rgba.len(), 22 * 22 * 4);
        assert_eq!(&svg.rgba[(11 * 22 + 11) * 4..][..4], &[255, 0, 0, 255]);
        let bitmap = rasterize(&tmp.path.join("icons/Parent/48x48/apps/bitmap.png"), 24).unwrap();
        assert_eq!(&bitmap.rgba[(12 * 24 + 12) * 4..][..4], &[0, 255, 0, 255]);
    }

    #[test]
    fn resolves_the_icon_named_by_the_desktop_entry() {
        let (tmp, themes) = fixture();
        tmp.write(
            "applications/org.example.Viewer.desktop",
            b"[Desktop Entry]\nType=Application\nName=Viewer\nExec=viewer\nIcon=vector\n",
        );
        let entries = DesktopEntryCache::new(vec![tmp.path.join("applications")]);
        let icon = load_app_icon("org.example.Viewer", Some(&entries), &themes, 20).unwrap();
        assert_eq!(&icon.rgba[(10 * 20 + 10) * 4..][..4], &[255, 0, 0, 255]);
    }

    #[test]
    fn unknown_apps_fall_back_to_the_generic_executable_icon() {
        let (_tmp, themes) = fixture();
        let icon = load_app_icon("no.such.App", None, &themes, 20).unwrap();
        assert_eq!(&icon.rgba[(10 * 20 + 10) * 4..][..4], &[9, 9, 9, 255]);
    }

    #[test]
    fn app_id_and_its_last_segment_are_tried_as_icon_names() {
        assert_eq!(
            icon_names("com.example.Bitmap", None),
            vec!["com.example.Bitmap", "com.example.bitmap", "bitmap"]
        );
        let (_tmp, themes) = fixture();
        let icon = load_app_icon("com.example.Bitmap", None, &themes, 48).unwrap();
        assert_eq!(&icon.rgba[(24 * 48 + 24) * 4..][..4], &[0, 255, 0, 255]);
    }

    #[test]
    fn an_absolute_icon_path_is_used_directly() {
        let (tmp, themes) = fixture();
        let path = tmp.path.join("pixmaps/legacy.png");
        assert_eq!(themes.find(path.to_str().unwrap(), 22), Some(path));
    }
}
