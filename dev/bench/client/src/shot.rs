//! Minimal grim: capture the first output with wlr-screencopy into a PNG.
//! Unlike `driftwm msg screenshot` (a canvas re-render), this is what the
//! output actually shows, layer-shell bars and popups included.
//!
//! Usage: drift-shot OUT.png

use std::os::fd::{AsFd, AsRawFd, FromRawFd, OwnedFd};

use wayland_client::protocol::{wl_buffer, wl_output, wl_registry, wl_shm, wl_shm_pool};
use wayland_client::{Connection, Dispatch, Proxy, QueueHandle, WEnum, delegate_noop};
use wayland_protocols_wlr::screencopy::v1::client::{
    zwlr_screencopy_frame_v1 as frame, zwlr_screencopy_manager_v1 as manager,
};

#[derive(Default)]
struct App {
    shm: Option<wl_shm::WlShm>,
    output: Option<wl_output::WlOutput>,
    manager: Option<manager::ZwlrScreencopyManagerV1>,
    format: Option<(wl_shm::Format, u32, u32, u32)>,
    buffer_done: bool,
    ready: bool,
    failed: bool,
    y_invert: bool,
}

fn main() {
    let out = std::env::args().nth(1).expect("usage: drift-shot OUT.png [--flip]");
    // The nested (winit) output is Flipped180 without reporting y_invert.
    let flip = std::env::args().any(|a| a == "--flip");
    let conn = Connection::connect_to_env().expect("no wayland display");
    let mut queue = conn.new_event_queue();
    let qh = queue.handle();
    conn.display().get_registry(&qh, ());
    let mut app = App::default();
    queue.roundtrip(&mut app).unwrap();
    let mgr = app.manager.clone().expect("compositor lacks wlr-screencopy");
    let output = app.output.clone().expect("no output");
    let shm = app.shm.clone().expect("no wl_shm");

    let fr = mgr.capture_output(0, &output, &qh, ());
    while app.format.is_none() || !app.buffer_done {
        if app.failed {
            panic!("capture failed");
        }
        queue.blocking_dispatch(&mut app).unwrap();
        if app.format.is_some() && fr.version() < 3 {
            break;
        }
    }
    let (fmt, w, h, stride) = app.format.unwrap();
    let size = (stride * h) as usize;
    let fd = unsafe {
        let fd = libc::memfd_create(c"drift-shot".as_ptr(), libc::MFD_CLOEXEC);
        assert!(fd >= 0);
        assert_eq!(libc::ftruncate(fd, size as libc::off_t), 0);
        OwnedFd::from_raw_fd(fd)
    };
    let pool = shm.create_pool(fd.as_fd(), size as i32, &qh, ());
    let buf = pool.create_buffer(0, w as i32, h as i32, stride as i32, fmt, &qh, ());
    fr.copy(&buf);
    while !app.ready {
        if app.failed {
            panic!("copy failed");
        }
        queue.blocking_dispatch(&mut app).unwrap();
    }
    let data = unsafe {
        let p = libc::mmap(std::ptr::null_mut(), size, libc::PROT_READ, libc::MAP_SHARED, fd.as_raw_fd(), 0);
        assert_ne!(p, libc::MAP_FAILED);
        std::slice::from_raw_parts(p as *const u8, size)
    };
    let mut rgba = Vec::with_capacity((w * h * 4) as usize);
    for row in 0..h {
        let src = if app.y_invert != flip { h - 1 - row } else { row };
        let line = &data[(src * stride) as usize..(src * stride + w * 4) as usize];
        for px in line.chunks_exact(4) {
            // wl_shm Argb/Xrgb8888 are little-endian: B, G, R, A in memory.
            let (r, g, b) = match fmt {
                wl_shm::Format::Abgr8888 | wl_shm::Format::Xbgr8888 => (px[0], px[1], px[2]),
                _ => (px[2], px[1], px[0]),
            };
            rgba.extend_from_slice(&[r, g, b, 255]);
        }
    }
    let file = std::io::BufWriter::new(std::fs::File::create(&out).unwrap());
    let mut enc = png::Encoder::new(file, w, h);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    enc.write_header().unwrap().write_image_data(&rgba).unwrap();
    println!("{out} {w}x{h}");
}

impl Dispatch<wl_registry::WlRegistry, ()> for App {
    fn event(app: &mut Self, reg: &wl_registry::WlRegistry, event: wl_registry::Event, _: &(), _: &Connection, qh: &QueueHandle<Self>) {
        if let wl_registry::Event::Global { name, interface, version } = event {
            match interface.as_str() {
                "wl_shm" => app.shm = Some(reg.bind(name, 1, qh, ())),
                "wl_output" if app.output.is_none() => app.output = Some(reg.bind(name, 1, qh, ())),
                "zwlr_screencopy_manager_v1" => app.manager = Some(reg.bind(name, version.min(3), qh, ())),
                _ => {}
            }
        }
    }
}

impl Dispatch<frame::ZwlrScreencopyFrameV1, ()> for App {
    fn event(app: &mut Self, _: &frame::ZwlrScreencopyFrameV1, event: frame::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        match event {
            frame::Event::Buffer { format: WEnum::Value(f), width, height, stride } => {
                if app.format.is_none() {
                    app.format = Some((f, width, height, stride));
                }
            }
            frame::Event::BufferDone => app.buffer_done = true,
            frame::Event::Flags { flags: WEnum::Value(f) } => app.y_invert = f.contains(frame::Flags::YInvert),
            frame::Event::Ready { .. } => app.ready = true,
            frame::Event::Failed => app.failed = true,
            _ => {}
        }
    }
}

delegate_noop!(App: ignore wl_shm::WlShm);
delegate_noop!(App: ignore wl_shm_pool::WlShmPool);
delegate_noop!(App: ignore wl_buffer::WlBuffer);
delegate_noop!(App: ignore wl_output::WlOutput);
delegate_noop!(App: ignore manager::ZwlrScreencopyManagerV1);
