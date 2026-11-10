// No panics outside tests: a compositor that aborts takes every client with
// it. Errors are logged and recovered; the few structural invariants left
// carry an `#[expect]` with the reason. See dev/docs/wonderlandwm/fase-1.md.
#![cfg_attr(
    not(test),
    deny(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::unreachable,
        clippy::todo,
        clippy::unimplemented
    )
)]
pub mod app_icon;
pub mod canvas;
pub mod close_effect;
pub mod config;
pub mod cursor_blur;
pub mod desktop_entry;
pub mod layout;
pub mod magic_lamp;
pub mod protocols;
pub mod session;
pub mod stage;
pub mod sync;
pub mod text;
pub mod text_cursor;
pub mod tilt;
pub mod window_ext;
pub mod wobbly;
