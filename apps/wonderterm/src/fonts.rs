use std::process::Command;
use std::sync::Arc;

use eframe::egui::{FontData, FontDefinitions, FontFamily};

pub const BOLD: &str = "term-bold";
pub const ITALIC: &str = "term-italic";
pub const BOLD_ITALIC: &str = "term-bold-italic";

const PREFERRED: &[&str] = &[
    "Adwaita Mono",
    "JetBrains Mono",
    "MesloLGS Nerd Font Mono",
    "DejaVu Sans Mono",
];
/// Icons and symbols the primary face lacks. The ".ttc" CJK collections are
/// tens of megabytes each, so they are capped by size rather than listed.
const FALLBACKS: &[&str] = &[
    "Symbols Nerd Font Mono",
    "MesloLGS Nerd Font Mono",
    "Noto Sans Mono",
    "Noto Sans Symbols 2",
    "Noto Sans Mono CJK JP",
];
const MAX_FALLBACK_BYTES: u64 = 40 << 20;

struct Match {
    file: String,
    family: String,
    index: u32,
}

fn fc_match(pattern: &str) -> Option<Match> {
    let out = Command::new("fc-match")
        .args(["-f", "%{file}\n%{family}\n%{index}", pattern])
        .output()
        .ok()?;
    let text = String::from_utf8(out.stdout).ok()?;
    let mut lines = text.lines();
    Some(Match {
        file: lines.next()?.to_owned(),
        family: lines.next()?.to_owned(),
        index: lines.next()?.parse().unwrap_or(0),
    })
}

/// fontconfig always answers with something; this keeps the answer only when
/// it is the family that was asked for.
fn exact(family: &str) -> Option<Match> {
    let m = fc_match(family)?;
    m.family
        .split(',')
        .any(|f| f.trim().eq_ignore_ascii_case(family))
        .then_some(m)
}

fn load(m: &Match, limit: Option<u64>) -> Option<FontData> {
    if let Some(limit) = limit
        && std::fs::metadata(&m.file).ok()?.len() > limit
    {
        return None;
    }
    let mut data = FontData::from_owned(std::fs::read(&m.file).ok()?);
    data.index = m.index;
    Some(data)
}

/// The terminal face (regular, bold, italic, bold italic) and the UI face.
/// egui's bundled fonts stay last in every chain, so a system without
/// fontconfig still renders.
pub fn definitions(wanted: &str) -> FontDefinitions {
    let mut defs = FontDefinitions::default();
    let builtin_mono = defs.families[&FontFamily::Monospace].clone();

    let primary = if wanted.is_empty() {
        PREFERRED.iter().find_map(|f| exact(f))
    } else {
        exact(wanted)
    }
    .or_else(|| fc_match("monospace"));
    let family = primary
        .as_ref()
        .map(|m| m.family.split(',').next().unwrap_or_default().to_owned())
        .unwrap_or_default();

    let add = |defs: &mut FontDefinitions, key: String, m: &Match, limit| -> bool {
        if defs.font_data.contains_key(&key) {
            return true;
        }
        match load(m, limit) {
            Some(data) => {
                defs.font_data.insert(key, Arc::new(data));
                true
            }
            None => false,
        }
    };

    let mut tail: Vec<String> = Vec::new();
    for name in FALLBACKS {
        if let Some(m) = exact(name) {
            let key = format!("fallback:{}:{}", m.file, m.index);
            if add(&mut defs, key.clone(), &m, Some(MAX_FALLBACK_BYTES)) && !tail.contains(&key) {
                tail.push(key);
            }
        }
    }
    tail.extend(builtin_mono);

    let styles = [
        (FontFamily::Monospace, ""),
        (FontFamily::Name(BOLD.into()), ":bold"),
        (FontFamily::Name(ITALIC.into()), ":italic"),
        (FontFamily::Name(BOLD_ITALIC.into()), ":bold:italic"),
    ];
    for (target, style) in styles {
        let mut chain = Vec::new();
        if !family.is_empty()
            && let Some(m) = fc_match(&format!("{family}{style}"))
        {
            let key = format!("term:{}:{}", m.file, m.index);
            if add(&mut defs, key.clone(), &m, None) {
                chain.push(key);
            }
        }
        chain.extend(tail.iter().cloned());
        defs.families.insert(target, chain);
    }

    if let Some(m) = exact("Noto Sans").or_else(|| fc_match("sans-serif")) {
        let key = format!("ui:{}:{}", m.file, m.index);
        if add(&mut defs, key.clone(), &m, Some(MAX_FALLBACK_BYTES)) {
            defs.families
                .entry(FontFamily::Proportional)
                .or_default()
                .insert(0, key);
        }
    }

    defs
}
