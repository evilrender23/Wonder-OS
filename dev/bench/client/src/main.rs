//! Baseline load generator: one process, N xdg-toplevels backed by wl_shm.
//!
//! Usage: drift-bench-client <count> [static|animated] [WxH]
//! `animated` redraws every window on each frame callback (worst case: every
//! client renders as fast as the compositor lets it); `static` draws once.

use std::os::fd::{AsFd, FromRawFd, OwnedFd};

use wayland_client::protocol::{
    wl_buffer, wl_callback, wl_compositor, wl_registry, wl_shm, wl_shm_pool, wl_surface,
};
use wayland_client::{Connection, Dispatch, QueueHandle, delegate_noop};
use wayland_protocols::xdg::shell::client::{xdg_surface, xdg_toplevel, xdg_wm_base};

struct Win {
    surface: wl_surface::WlSurface,
    xdg: xdg_surface::XdgSurface,
    _top: xdg_toplevel::XdgToplevel,
    buffers: Vec<(wl_buffer::WlBuffer, *mut u32)>,
    w: i32,
    h: i32,
    frame: u32,
    configured: bool,
    hue: u32,
}

struct App {
    compositor: Option<wl_compositor::WlCompositor>,
    shm: Option<wl_shm::WlShm>,
    wm: Option<xdg_wm_base::XdgWmBase>,
    wins: Vec<Win>,
    animated: bool,
    frames_drawn: u64,
}

fn memfd(size: usize) -> OwnedFd {
    unsafe {
        let fd = libc::memfd_create(c"drift-bench".as_ptr(), libc::MFD_CLOEXEC);
        assert!(fd >= 0, "memfd_create failed");
        assert_eq!(libc::ftruncate(fd, size as libc::off_t), 0);
        OwnedFd::from_raw_fd(fd)
    }
}

impl App {
    fn draw(&mut self, idx: usize, qh: &QueueHandle<Self>) {
        let animated = self.animated;
        let win = &mut self.wins[idx];
        let slot = (win.frame % 2) as usize;
        let (buf, ptr) = &win.buffers[slot];
        let (w, h) = (win.w as usize, win.h as usize);
        let px = unsafe { std::slice::from_raw_parts_mut(*ptr, w * h) };
        let band = (win.frame as usize * 4) % w.max(1);
        let base = 0xff00_0000 | (win.hue & 0x00ff_ffff);
        for y in 0..h {
            let row = &mut px[y * w..(y + 1) * w];
            for (x, p) in row.iter_mut().enumerate() {
                let stripe = ((x + band) / 32 + y / 32) % 2 == 0;
                *p = if stripe { base } else { base ^ 0x0030_3030 };
            }
        }
        win.surface.attach(Some(buf), 0, 0);
        win.surface.damage_buffer(0, 0, win.w, win.h);
        if animated {
            win.surface.frame(qh, idx);
        }
        win.surface.commit();
        win.frame = win.frame.wrapping_add(1);
        self.frames_drawn += 1;
    }
}

fn main() {
    let mut args = std::env::args().skip(1);
    let count: usize = args.next().and_then(|s| s.parse().ok()).unwrap_or(10);
    let animated = args.next().as_deref() == Some("animated");
    let (w, h) = args
        .next()
        .and_then(|s| {
            let (a, b) = s.split_once('x')?;
            Some((a.parse().ok()?, b.parse().ok()?))
        })
        .unwrap_or((640, 400));

    let conn = Connection::connect_to_env().expect("no wayland display");
    let mut queue = conn.new_event_queue();
    let qh = queue.handle();
    conn.display().get_registry(&qh, ());
    let mut app = App {
        compositor: None,
        shm: None,
        wm: None,
        wins: Vec::new(),
        animated,
        frames_drawn: 0,
    };
    queue.roundtrip(&mut app).unwrap();
    let compositor = app.compositor.clone().expect("wl_compositor");
    let shm = app.shm.clone().expect("wl_shm");
    let wm = app.wm.clone().expect("xdg_wm_base");

    for i in 0..count {
        let stride = w * 4;
        let size = (stride * h * 2) as usize;
        let fd = memfd(size);
        let ptr = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                size,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                std::os::fd::AsRawFd::as_raw_fd(&fd),
                0,
            )
        };
        assert_ne!(ptr, libc::MAP_FAILED);
        let pool = shm.create_pool(fd.as_fd(), size as i32, &qh, ());
        let buffers = (0..2)
            .map(|b| {
                let off = b * stride * h;
                let buf = pool.create_buffer(off, w, h, stride, wl_shm::Format::Argb8888, &qh, ());
                (buf, unsafe { (ptr as *mut u32).add((off / 4) as usize) })
            })
            .collect();
        pool.destroy();
        let surface = compositor.create_surface(&qh, ());
        let xdg = wm.get_xdg_surface(&surface, &qh, i);
        let top = xdg.get_toplevel(&qh, ());
        top.set_app_id("drift-bench".into());
        top.set_title(format!("bench {i}"));
        surface.commit();
        app.wins.push(Win {
            surface,
            xdg,
            _top: top,
            buffers,
            w,
            h,
            frame: 0,
            configured: false,
            hue: (i as u32).wrapping_mul(0x9e37_79b9) >> 8,
        });
        // Keep mapping paced so placement and animations settle like real use.
        if i % 10 == 9 {
            queue.roundtrip(&mut app).unwrap();
        }
    }
    eprintln!("drift-bench-client: {count} windows, animated={animated}, {w}x{h}");
    let start = std::time::Instant::now();
    let mut last = 0u64;
    loop {
        queue.blocking_dispatch(&mut app).unwrap();
        if start.elapsed().as_secs() > last {
            last = start.elapsed().as_secs();
            if last % 10 == 0 {
                eprintln!("t={last}s frames_drawn={}", app.frames_drawn);
            }
        }
    }
}

impl Dispatch<wl_registry::WlRegistry, ()> for App {
    fn event(
        app: &mut Self,
        reg: &wl_registry::WlRegistry,
        event: wl_registry::Event,
        _: &(),
        _: &Connection,
        qh: &QueueHandle<Self>,
    ) {
        if let wl_registry::Event::Global { name, interface, .. } = event {
            match interface.as_str() {
                "wl_compositor" => app.compositor = Some(reg.bind(name, 4, qh, ())),
                "wl_shm" => app.shm = Some(reg.bind(name, 1, qh, ())),
                "xdg_wm_base" => app.wm = Some(reg.bind(name, 2, qh, ())),
                _ => {}
            }
        }
    }
}

impl Dispatch<xdg_wm_base::XdgWmBase, ()> for App {
    fn event(
        _: &mut Self,
        wm: &xdg_wm_base::XdgWmBase,
        event: xdg_wm_base::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let xdg_wm_base::Event::Ping { serial } = event {
            wm.pong(serial);
        }
    }
}

impl Dispatch<xdg_surface::XdgSurface, usize> for App {
    fn event(
        app: &mut Self,
        _: &xdg_surface::XdgSurface,
        event: xdg_surface::Event,
        idx: &usize,
        _: &Connection,
        qh: &QueueHandle<Self>,
    ) {
        if let xdg_surface::Event::Configure { serial } = event {
            let win = &mut app.wins[*idx];
            win.xdg.ack_configure(serial);
            if !win.configured {
                win.configured = true;
                app.draw(*idx, qh);
            } else {
                win.surface.commit();
            }
        }
    }
}

impl Dispatch<wl_callback::WlCallback, usize> for App {
    fn event(
        app: &mut Self,
        _: &wl_callback::WlCallback,
        event: wl_callback::Event,
        idx: &usize,
        _: &Connection,
        qh: &QueueHandle<Self>,
    ) {
        if let wl_callback::Event::Done { .. } = event {
            app.draw(*idx, qh);
        }
    }
}

delegate_noop!(App: ignore wl_compositor::WlCompositor);
delegate_noop!(App: ignore wl_shm::WlShm);
delegate_noop!(App: ignore wl_shm_pool::WlShmPool);
delegate_noop!(App: ignore wl_buffer::WlBuffer);
delegate_noop!(App: ignore wl_surface::WlSurface);
delegate_noop!(App: ignore xdg_toplevel::XdgToplevel);
