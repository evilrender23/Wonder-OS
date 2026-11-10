//! Slow-gated: the shell's desktop icons (`extras/quickshell/desktop`) run by a
//! real Quickshell against the fixture's socket, through a monitor that drops
//! out and comes back.
//!
//! A disconnect closes every layer surface on the output. The bar and the dock
//! are built per screen, so they return with it; the desktop is one surface on
//! the primary screen and has to do the same, or the icons are gone until the
//! shell restarts.
//!
//! `#[ignore]`d out of the default lane (spawns a real process); run with
//! `cargo test -- --include-ignored`. Self-skips when `qs` is not installed.

use std::os::unix::fs::PermissionsExt;
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

use super::real::TempDir;
use super::{Fixture, config};
use crate::state::DriftWm;

const NAMESPACE: &str = "wonderland-desktop";

struct KillOnDrop(Child);

impl Drop for KillOnDrop {
    fn drop(&mut self) {
        self.0.kill().ok();
        self.0.wait().ok();
    }
}

fn on_path(bin: &str) -> bool {
    std::env::var_os("PATH").is_some_and(|path| {
        std::env::split_paths(&path).any(|dir| {
            std::fs::metadata(dir.join(bin))
                .map(|m| m.is_file() && m.permissions().mode() & 0o111 != 0)
                .unwrap_or(false)
        })
    })
}

/// Name of the output that hosts the desktop's layer surface, if any does.
fn desktop_output(state: &DriftWm) -> Option<String> {
    state.space.outputs().find_map(|output| {
        smithay::desktop::layer_map_for_output(output)
            .layers()
            .any(|layer| layer.namespace() == NAMESPACE)
            .then(|| output.name())
    })
}

/// The shell's own files with a `shell.qml` that loads the desktop alone.
fn desktop_only_shell(dir: &Path) {
    let source = Path::new(env!("CARGO_MANIFEST_DIR")).join("extras/quickshell");
    let copied = Command::new("cp")
        .arg("-r")
        .arg(&source)
        .arg(dir)
        .status()
        .expect("copy the shell");
    assert!(copied.success(), "copy the shell");
    std::fs::write(
        dir.join("shell.qml"),
        "import Quickshell\nimport \"desktop\"\n\nShellRoot {\n    Desktop {}\n}\n",
    )
    .expect("write shell.qml");
}

#[test]
#[ignore = "spawns a real Quickshell; needs qs installed"]
fn desktop_icons_return_with_their_monitor() {
    if !on_path("qs") {
        eprintln!("skipping desktop_icons_return_with_their_monitor: qs not found on PATH");
        return;
    }

    let temp = TempDir::new();
    let dir = temp.path();
    std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o700)).unwrap();
    let shell = dir.join("shell");
    desktop_only_shell(&shell);
    std::fs::create_dir_all(dir.join("Desktop/A folder")).unwrap();

    // Placed by the config, as on a real desk: the primary is the screen at
    // the origin, wherever it comes in the list.
    let mut f = Fixture::with_config(config(
        r#"
[[outputs]]
name = "HEADLESS-1"
position = [0, 0]

[[outputs]]
name = "HEADLESS-2"
position = [1920, 0]
"#,
    ));
    let primary = f.add_output(1, (1920, 1080));
    f.add_output(2, (1280, 720));
    let socket = f.listen(dir);

    let log = std::fs::File::create(dir.join("qs.log")).unwrap();
    let _shell = KillOnDrop(
        Command::new("qs")
            .arg("-p")
            .arg(&shell)
            .env("WAYLAND_DISPLAY", &socket)
            .env("QT_QPA_PLATFORM", "wayland")
            .env("QT_QUICK_BACKEND", "software")
            .env("XDG_RUNTIME_DIR", dir)
            .env("XDG_STATE_HOME", dir.join("state"))
            .env("XDG_CONFIG_HOME", dir.join("config"))
            .env("XDG_CACHE_HOME", dir.join("cache"))
            .env("WONDERLAND_DESKTOP_DIR", dir.join("Desktop"))
            .env_remove("DISPLAY")
            .stdin(Stdio::null())
            .stdout(log.try_clone().unwrap())
            .stderr(log)
            .spawn()
            .expect("spawn qs"),
    );

    wait_for_desktop(&mut f, dir, "at startup");

    // The primary monitor blinks, as one does on a mode switch or a loose cable.
    f.remove_output(&primary);
    for _ in 0..40 {
        f.wait_until(Duration::from_secs(1), |_| true);
        std::thread::sleep(Duration::from_millis(5));
    }
    f.add_output(1, (1920, 1080));

    wait_for_desktop(&mut f, dir, "after its monitor came back");
}

/// Dispatches until the desktop's surface is on the primary output, or fails
/// with what the shell logged.
fn wait_for_desktop(f: &mut Fixture, dir: &Path, when: &str) {
    let deadline = Instant::now() + Duration::from_secs(20);
    loop {
        f.wait_until(Duration::from_secs(1), |_| true);
        if desktop_output(f.state()).as_deref() == Some("HEADLESS-1") {
            return;
        }
        if Instant::now() >= deadline {
            let log = std::fs::read_to_string(dir.join("qs.log")).unwrap_or_default();
            panic!(
                "no desktop surface on HEADLESS-1 {when} (it is on {:?}); the shell said:\n{log}",
                desktop_output(f.state())
            );
        }
        std::thread::sleep(Duration::from_millis(5));
    }
}
