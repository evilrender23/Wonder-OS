//! Box-drawing, block and Powerline glyphs painted as geometry, so borders
//! and prompt segments meet edge to edge whatever the font's metrics are.

use eframe::egui::{Color32, Painter, Pos2, Rect, Shape, Stroke, pos2};

use crate::theme::alpha;

/// Arms of a line-drawing character as (up, right, down, left): 0 none,
/// 1 light, 2 heavy.
fn arms(c: char) -> Option<[u8; 4]> {
    Some(match c {
        '─' => [0, 1, 0, 1],
        '━' => [0, 2, 0, 2],
        '│' => [1, 0, 1, 0],
        '┃' => [2, 0, 2, 0],
        '┌' => [0, 1, 1, 0],
        '┐' => [0, 0, 1, 1],
        '└' => [1, 1, 0, 0],
        '┘' => [1, 0, 0, 1],
        '├' => [1, 1, 1, 0],
        '┤' => [1, 0, 1, 1],
        '┬' => [0, 1, 1, 1],
        '┴' => [1, 1, 0, 1],
        '┼' => [1, 1, 1, 1],
        '┏' => [0, 2, 2, 0],
        '┓' => [0, 0, 2, 2],
        '┗' => [2, 2, 0, 0],
        '┛' => [2, 0, 0, 2],
        '┣' => [2, 2, 2, 0],
        '┫' => [2, 0, 2, 2],
        '┳' => [0, 2, 2, 2],
        '┻' => [2, 2, 0, 2],
        '╋' => [2, 2, 2, 2],
        '╴' => [0, 0, 0, 1],
        '╵' => [1, 0, 0, 0],
        '╶' => [0, 1, 0, 0],
        '╷' => [0, 0, 1, 0],
        '╸' => [0, 0, 0, 2],
        '╹' => [2, 0, 0, 0],
        '╺' => [0, 2, 0, 0],
        '╻' => [0, 0, 2, 0],
        _ => return None,
    })
}

/// Sub-rectangle of the cell in eighths: (x0, y0, x1, y1).
fn block(c: char) -> Option<[u8; 4]> {
    Some(match c {
        '▀' => [0, 0, 8, 4],
        '▁'..='█' => [0, 8 - (c as u32 - 0x2580) as u8, 8, 8],
        '▉'..='▏' => [0, 0, 8 - (c as u32 - 0x2588) as u8, 8],
        '▐' => [4, 0, 8, 8],
        '▔' => [0, 0, 8, 1],
        '▕' => [7, 0, 8, 8],
        '▖' => [0, 4, 4, 8],
        '▗' => [4, 4, 8, 8],
        '▘' => [0, 0, 4, 4],
        '▝' => [4, 0, 8, 4],
        _ => return None,
    })
}

#[cfg(test)]
fn handles(c: char) -> bool {
    arms(c).is_some()
        || block(c).is_some()
        || matches!(
            c,
            '░' | '▒' | '▓' | '╭' | '╮' | '╯' | '╰' | '\u{e0b0}' | '\u{e0b2}'
        )
}

/// Paint `c` into `cell`. `pixel` is one physical pixel in points. Returns
/// false for characters this module leaves to the font.
pub fn paint(painter: &Painter, c: char, cell: Rect, color: Color32, pixel: f32) -> bool {
    let snap = |v: f32| (v / pixel).round() * pixel;
    let light = (cell.width() / 8.0 / pixel).round().max(1.0) * pixel;

    if let Some(arms) = arms(c) {
        let center = cell.center();
        for (i, weight) in arms.into_iter().enumerate() {
            if weight == 0 {
                continue;
            }
            let t = light * weight as f32;
            // Reach past the centre by half the thickest arm so corners close.
            let reach = light * *arms.iter().max().unwrap() as f32 / 2.0;
            let (x0, x1) = (snap(center.x - t / 2.0), snap(center.x - t / 2.0) + t);
            let (y0, y1) = (snap(center.y - t / 2.0), snap(center.y - t / 2.0) + t);
            let rect = match i {
                0 => Rect::from_min_max(pos2(x0, cell.top()), pos2(x1, snap(center.y + reach))),
                1 => Rect::from_min_max(pos2(snap(center.x - reach), y0), pos2(cell.right(), y1)),
                2 => Rect::from_min_max(pos2(x0, snap(center.y - reach)), pos2(x1, cell.bottom())),
                _ => Rect::from_min_max(pos2(cell.left(), y0), pos2(snap(center.x + reach), y1)),
            };
            painter.rect_filled(rect, 0.0, color);
        }
        return true;
    }

    if let Some([x0, y0, x1, y1]) = block(c) {
        let at = |x: u8, y: u8| {
            pos2(
                snap(cell.left() + cell.width() * x as f32 / 8.0),
                snap(cell.top() + cell.height() * y as f32 / 8.0),
            )
        };
        painter.rect_filled(Rect::from_min_max(at(x0, y0), at(x1, y1)), 0.0, color);
        return true;
    }

    match c {
        '░' | '▒' | '▓' => {
            let shade = [0.25, 0.5, 0.75][(c as u32 - 0x2591) as usize];
            painter.rect_filled(cell, 0.0, alpha(color, shade * color.a() as f32 / 255.0));
        }
        '╭' | '╮' | '╯' | '╰' => {
            // (horizontal arm direction, vertical arm direction)
            let (dx, dy) = match c {
                '╭' => (1.0, 1.0),
                '╮' => (-1.0, 1.0),
                '╯' => (-1.0, -1.0),
                _ => (1.0, -1.0),
            };
            let half = light / 2.0;
            let center = pos2(
                snap(cell.center().x - half) + half,
                snap(cell.center().y - half) + half,
            );
            let radius = cell.width().min(cell.height()) / 2.0;
            let pivot = pos2(center.x + dx * radius, center.y + dy * radius);
            let vertical_end = if dy > 0.0 { cell.bottom() } else { cell.top() };
            let horizontal_end = if dx > 0.0 { cell.right() } else { cell.left() };
            let mut points: Vec<Pos2> = vec![pos2(center.x, vertical_end)];
            for step in 0..=8 {
                let a = step as f32 / 8.0 * std::f32::consts::FRAC_PI_2;
                points.push(pos2(
                    pivot.x - dx * radius * a.cos(),
                    pivot.y - dy * radius * a.sin(),
                ));
            }
            points.push(pos2(horizontal_end, center.y));
            painter.add(Shape::line(points, Stroke::new(light, color)));
        }
        '\u{e0b0}' => {
            let tip = pos2(cell.right(), cell.center().y);
            let points = vec![cell.left_top(), tip, cell.left_bottom()];
            painter.add(Shape::convex_polygon(points, color, Stroke::NONE));
        }
        '\u{e0b2}' => {
            let tip = pos2(cell.left(), cell.center().y);
            let points = vec![cell.right_top(), cell.right_bottom(), tip];
            painter.add(Shape::convex_polygon(points, color, Stroke::NONE));
        }
        _ => return false,
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn eighth_blocks_cover_the_right_part_of_the_cell() {
        assert_eq!(block('█'), Some([0, 0, 8, 8]));
        assert_eq!(block('▄'), Some([0, 4, 8, 8]));
        assert_eq!(block('▁'), Some([0, 7, 8, 8]));
        assert_eq!(block('▌'), Some([0, 0, 4, 8]));
        assert_eq!(block('▏'), Some([0, 0, 1, 8]));
    }

    #[test]
    fn only_drawable_characters_are_claimed() {
        for c in "─│┌┐└┘├┤┬┴┼━┃╭╮╯╰█▀▄░▒▓\u{e0b0}\u{e0b2}".chars()
        {
            assert!(handles(c), "{c}");
        }
        for c in "aé═║╔┄❯\u{e0b1}".chars() {
            assert!(!handles(c), "{c}");
        }
    }
}
