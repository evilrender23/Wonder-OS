use std::collections::HashMap;
use std::fs::File;
use std::os::fd::AsRawFd;
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::mpsc::{Receiver, Sender, channel};

use alacritty_terminal::event::{Event, EventListener, Notify, OnResize, WindowSize};
use alacritty_terminal::event_loop::{EventLoop, Msg, Notifier};
use alacritty_terminal::grid::Dimensions;
use alacritty_terminal::sync::FairMutex;
use alacritty_terminal::term::{Config as TermConfig, Term};
use alacritty_terminal::tty;
use alacritty_terminal::vte::ansi::{CursorShape, CursorStyle};
use eframe::egui::Context;

use crate::config::{Config, Shape};
use crate::cursor::CursorAnim;

#[derive(Clone)]
pub struct Proxy {
    tx: Sender<Event>,
    ctx: Context,
}

impl EventListener for Proxy {
    fn send_event(&self, event: Event) {
        let _ = self.tx.send(event);
        self.ctx.request_repaint();
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct GridSize {
    pub columns: usize,
    pub lines: usize,
}

impl Dimensions for GridSize {
    fn total_lines(&self) -> usize {
        self.lines
    }

    fn screen_lines(&self) -> usize {
        self.lines
    }

    fn columns(&self) -> usize {
        self.columns
    }
}

pub struct Launch {
    pub command: Vec<String>,
    pub cwd: Option<PathBuf>,
}

/// One tab: a terminal grid, the PTY feeding it, and its cursor animation.
pub struct Session {
    pub term: Arc<FairMutex<Term<Proxy>>>,
    pub events: Receiver<Event>,
    pub title: String,
    pub cursor: CursorAnim,
    pub bell_at: Option<f64>,
    pub scroll_remainder: f32,
    notifier: Notifier,
    pty: File,
    child: u32,
    size: GridSize,
    cell: (u16, u16),
}

impl Session {
    pub fn spawn(
        id: u64,
        cfg: &Config,
        launch: &Launch,
        size: GridSize,
        cell: (u16, u16),
        ctx: &Context,
    ) -> std::io::Result<Self> {
        let shell = if let Some((program, args)) = launch.command.split_first() {
            Some(tty::Shell::new(program.clone(), args.to_vec()))
        } else if !cfg.terminal.shell.is_empty() {
            Some(tty::Shell::new(
                cfg.terminal.shell.clone(),
                cfg.terminal.args.clone(),
            ))
        } else {
            None
        };
        let env = HashMap::from([
            ("TERM".to_owned(), cfg.terminal.term.clone()),
            ("COLORTERM".to_owned(), "truecolor".to_owned()),
            ("TERM_PROGRAM".to_owned(), "wonderterm".to_owned()),
            (
                "TERM_PROGRAM_VERSION".to_owned(),
                env!("CARGO_PKG_VERSION").to_owned(),
            ),
        ]);
        let options = tty::Options {
            shell,
            working_directory: launch.cwd.clone().filter(|p| p.is_dir()),
            drain_on_exit: true,
            env,
        };
        let pty = tty::new(&options, window_size(size, cell), id)?;
        let child = pty.child().id();
        let pty_file = pty.file().try_clone()?;

        let (tx, events) = channel();
        let proxy = Proxy {
            tx,
            ctx: ctx.clone(),
        };
        let term_config = TermConfig {
            scrolling_history: cfg.terminal.scrollback,
            default_cursor_style: CursorStyle {
                shape: match cfg.cursor.shape {
                    Shape::Block => CursorShape::Block,
                    Shape::Beam => CursorShape::Beam,
                    Shape::Underline => CursorShape::Underline,
                },
                blinking: cfg.cursor.blink,
            },
            ..Default::default()
        };
        let term = Arc::new(FairMutex::new(Term::new(term_config, &size, proxy.clone())));
        let event_loop = EventLoop::new(term.clone(), proxy, pty, true, false)?;
        let notifier = Notifier(event_loop.channel());
        event_loop.spawn();

        Ok(Self {
            term,
            events,
            title: String::new(),
            cursor: CursorAnim::default(),
            bell_at: None,
            scroll_remainder: 0.0,
            notifier,
            pty: pty_file,
            child,
            size,
            cell,
        })
    }

    pub fn write(&self, bytes: impl Into<Vec<u8>>) {
        self.notifier.notify(bytes.into());
    }

    pub fn resize(&mut self, size: GridSize, cell: (u16, u16)) {
        if size == self.size && cell == self.cell {
            return;
        }
        self.size = size;
        self.cell = cell;
        self.term.lock().resize(size);
        self.notifier.on_resize(window_size(size, cell));
    }

    pub fn window_size(&self) -> WindowSize {
        window_size(self.size, self.cell)
    }

    /// The process that owns the terminal right now (vim inside the shell,
    /// not the shell), falling back to the shell itself.
    fn foreground_pid(&self) -> u32 {
        let pgid = unsafe { libc::tcgetpgrp(self.pty.as_raw_fd()) };
        if pgid > 0 { pgid as u32 } else { self.child }
    }

    /// Whether the kernel is editing the line (`cat`, `read`) rather than the
    /// program (a shell prompt, a REPL): arrow keys mean nothing then.
    pub fn canonical(&self) -> bool {
        let mut termios = std::mem::MaybeUninit::<libc::termios>::uninit();
        // SAFETY: the descriptor is ours and open; tcgetattr fills the struct
        // when it returns 0.
        unsafe {
            libc::tcgetattr(self.pty.as_raw_fd(), termios.as_mut_ptr()) == 0
                && termios.assume_init().c_lflag & libc::ICANON != 0
        }
    }

    pub fn foreground_name(&self) -> String {
        std::fs::read_to_string(format!("/proc/{}/comm", self.foreground_pid()))
            .map(|s| s.trim().to_owned())
            .unwrap_or_default()
    }

    pub fn cwd(&self) -> Option<PathBuf> {
        std::fs::read_link(format!("/proc/{}/cwd", self.foreground_pid()))
            .or_else(|_| std::fs::read_link(format!("/proc/{}/cwd", self.child)))
            .ok()
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        let _ = self.notifier.0.send(Msg::Shutdown);
    }
}

fn window_size(size: GridSize, cell: (u16, u16)) -> WindowSize {
    WindowSize {
        num_lines: size.lines as u16,
        num_cols: size.columns as u16,
        cell_width: cell.0,
        cell_height: cell.1,
    }
}
