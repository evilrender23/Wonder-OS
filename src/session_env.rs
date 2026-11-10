//! Publishing session variables to the systemd user manager and the D-Bus
//! activation environment, where portals, user services and D-Bus-activated
//! apps pick up the displays they connect to.
//!
//! That environment is one per user, not one per compositor: only the instance
//! that is the login session may write to it. A nested instance that published
//! its own `WAYLAND_DISPLAY`/`DISPLAY` would repoint everything started later
//! in the real session at displays that vanish with the nested window, and X11
//! apps would then fail with "check your DISPLAY".

use std::process::Command;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SessionEnv {
    /// This instance is the login session: its displays are the user's.
    Publish,
    /// Nested inside another session (or a test): children get the variables
    /// through `child_env`, nothing outside this process tree hears of them.
    Private,
}

impl SessionEnv {
    /// The udev backend owns the seat, so it is the session. winit always runs
    /// as a client of some other compositor.
    pub fn for_backend(backend: &str) -> Self {
        if backend == "udev" {
            Self::Publish
        } else {
            Self::Private
        }
    }

    /// Deliberately synchronous: autostart, and whatever it starts through the
    /// user manager, must find the variables already exported. Startup-only,
    /// and bounded by systemctl's own bus timeout.
    pub fn publish(self, vars: &[(&str, &str)]) {
        let Some(mut command) = self.import_command(vars) else {
            return;
        };
        match command.spawn() {
            Ok(mut child) => {
                if let Err(e) = child.wait() {
                    tracing::warn!("Error waiting for environment import: {e}");
                }
            }
            Err(e) => tracing::warn!("Failed to import environment: {e}"),
        }
    }

    /// The values travel through `Command::env` rather than the process env:
    /// the policy is "don't touch process env at runtime", so the shell-out
    /// gets only what we hand it.
    fn import_command(self, vars: &[(&str, &str)]) -> Option<Command> {
        if self == Self::Private || vars.is_empty() {
            return None;
        }
        let names = vars.iter().map(|(k, _)| *k).collect::<Vec<_>>().join(" ");
        let script = format!(
            "systemctl --user import-environment {names}; \
             hash dbus-update-activation-environment 2>/dev/null && \
             dbus-update-activation-environment {names}"
        );
        let mut command = Command::new("/bin/sh");
        command.args(["-c", &script]).envs(vars.iter().copied());
        Some(command)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const DISPLAYS: [(&str, &str); 2] = [("WAYLAND_DISPLAY", "wayland-2"), ("DISPLAY", ":1")];

    #[test]
    fn only_the_seat_owning_backend_is_the_session() {
        assert_eq!(SessionEnv::for_backend("udev"), SessionEnv::Publish);
        assert_eq!(SessionEnv::for_backend("winit"), SessionEnv::Private);
    }

    /// A nested run used to leave the user manager pointing at its own
    /// `wayland-2` / `:1`, which broke every X11 app launched afterwards.
    #[test]
    fn a_nested_instance_has_nothing_to_tell_the_user_manager() {
        assert!(SessionEnv::Private.import_command(&DISPLAYS).is_none());
    }

    #[test]
    fn the_session_imports_exactly_the_variables_it_was_given() {
        let command = SessionEnv::Publish.import_command(&DISPLAYS).unwrap();
        let script = command.get_args().nth(1).unwrap().to_str().unwrap();
        assert!(script.contains("import-environment WAYLAND_DISPLAY DISPLAY;"));
        assert!(script.contains("dbus-update-activation-environment WAYLAND_DISPLAY DISPLAY"));
        let env: Vec<_> = command
            .get_envs()
            .map(|(k, v)| (k.to_str().unwrap(), v.unwrap().to_str().unwrap()))
            .collect();
        assert_eq!(env, [("DISPLAY", ":1"), ("WAYLAND_DISPLAY", "wayland-2")]);
    }
}
