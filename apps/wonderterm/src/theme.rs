use eframe::egui::Color32;

/// The Hermes "nous" palette the WonderlandWM shell uses (extras/quickshell/Theme.qml),
/// plus sixteen ANSI colours tuned to sit on its surfaces.
#[derive(Clone, Debug, PartialEq)]
pub struct Theme {
    pub dark: bool,
    pub bg: Color32,
    pub fg: Color32,
    pub chrome: Color32,
    pub surface: Color32,
    pub text2: Color32,
    pub text3: Color32,
    pub stroke: Color32,
    pub hover: Color32,
    pub accent: Color32,
    pub on_accent: Color32,
    pub red: Color32,
    pub yellow: Color32,
    pub green: Color32,
    pub ansi: [Color32; 16],
}

const fn hex(v: u32) -> Color32 {
    Color32::from_rgb((v >> 16) as u8, (v >> 8) as u8, v as u8)
}

impl Theme {
    pub fn new(dark: bool) -> Self {
        if dark {
            Self {
                dark,
                bg: hex(0x0d1117),
                fg: hex(0xe6edf3),
                chrome: hex(0x010409),
                surface: hex(0x161b22),
                text2: hex(0xb1bac4),
                text3: hex(0x7d8590),
                stroke: hex(0x30363d),
                hover: hex(0x17243a),
                accent: hex(0x4a84fe),
                on_accent: hex(0x0d1117),
                red: hex(0xf85149),
                yellow: hex(0xd29922),
                green: hex(0x3fb950),
                ansi: [
                    0x484f58, 0xff7b72, 0x3fb950, 0xd29922, 0x4a84fe, 0xbc8cff, 0x39c5cf, 0xb1bac4,
                    0x6e7681, 0xffa198, 0x56d364, 0xe3b341, 0x79a6ff, 0xd2a8ff, 0x56d4dd, 0xf0f6fc,
                ]
                .map(hex),
            }
        } else {
            Self {
                dark,
                bg: hex(0xfdfdfd),
                fg: hex(0x242528),
                chrome: hex(0xf3f7ff),
                surface: hex(0xffffff),
                text2: hex(0x515255),
                text3: hex(0x6b6c70),
                stroke: hex(0xc3d1f0),
                hover: hex(0xe7ecf7),
                accent: hex(0x0053fd),
                on_accent: hex(0xfcfcfc),
                red: hex(0xcf2d56),
                yellow: hex(0xc08532),
                green: hex(0x1f8a65),
                ansi: [
                    0x242528, 0xcf2d56, 0x1f8a65, 0x9a6700, 0x0053fd, 0x8250df, 0x1b7c83, 0x7e7f83,
                    0x515255, 0xa40e26, 0x1a7f37, 0x7d5500, 0x0a45d6, 0x6639ba, 0x146a70, 0xa7a8ac,
                ]
                .map(hex),
            }
        }
    }

    /// The xterm 256-colour table with this theme's first sixteen.
    pub fn indexed(&self, i: u8) -> Color32 {
        match i {
            0..=15 => self.ansi[i as usize],
            16..=231 => {
                let i = i - 16;
                let level = |n: u8| if n == 0 { 0 } else { 55 + 40 * n };
                Color32::from_rgb(level(i / 36), level(i / 6 % 6), level(i % 6))
            }
            _ => {
                let v = 8 + 10 * (i - 232);
                Color32::from_rgb(v, v, v)
            }
        }
    }

    /// `index` is alacritty_terminal's colour slot: 0..256 indexed, then
    /// foreground, background, cursor, eight dim colours, bright and dim foreground.
    pub fn slot(&self, index: usize) -> Color32 {
        match index {
            0..=255 => self.indexed(index as u8),
            256 | 267 => self.fg,
            257 => self.bg,
            258 => self.accent,
            259..=266 => mix(self.ansi[index - 259], self.bg, 0.35),
            _ => mix(self.fg, self.bg, 0.35),
        }
    }
}

pub fn mix(a: Color32, b: Color32, t: f32) -> Color32 {
    let l = |x: u8, y: u8| (x as f32 + (y as f32 - x as f32) * t).round() as u8;
    Color32::from_rgb(l(a.r(), b.r()), l(a.g(), b.g()), l(a.b(), b.b()))
}

pub fn alpha(c: Color32, a: f32) -> Color32 {
    Color32::from_rgba_unmultiplied(
        c.r(),
        c.g(),
        c.b(),
        (a.clamp(0.0, 1.0) * 255.0).round() as u8,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colour_cube_and_grey_ramp_match_xterm() {
        let t = Theme::new(true);
        assert_eq!(t.indexed(16), Color32::from_rgb(0, 0, 0));
        assert_eq!(t.indexed(196), Color32::from_rgb(255, 0, 0));
        assert_eq!(t.indexed(231), Color32::from_rgb(255, 255, 255));
        assert_eq!(t.indexed(232), Color32::from_rgb(8, 8, 8));
        assert_eq!(t.indexed(255), Color32::from_rgb(238, 238, 238));
    }

    #[test]
    fn named_slots_resolve_to_the_palette() {
        let t = Theme::new(false);
        assert_eq!(t.slot(256), t.fg);
        assert_eq!(t.slot(257), t.bg);
        assert_eq!(t.slot(1), t.ansi[1]);
    }
}
