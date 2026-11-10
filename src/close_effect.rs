//! The close animations a window can leave with: the plain fade, and a set of
//! shader effects in the spirit of the Burn My Windows extension. Pure data —
//! which effects exist, what they are called in the config, how long each one
//! runs — so the config, the renderer and the settings app agree on one list.

/// One way for a closing window to disappear.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CloseEffect {
    /// Shrink a little and fade: the eased chase every other window animation
    /// runs on. The only effect that needs no shader.
    Fade,
    /// Burns away from the bottom edge up, flames licking ahead of the front.
    Fire,
    /// A scorch spreads from one point of the border and eats the window.
    Incinerate,
    /// An old CRT switching off: squashed to a line, then to a dot.
    Tv,
    /// Tilts back and slides away.
    Glide,
    /// Breaks into ever larger blocks that drop out one by one.
    Pixelate,
    /// Splits into glowing hexagonal tiles that shrink to nothing.
    Hexagon,
    /// Beamed up: dissolves into rising streaks of light.
    Energize,
    /// Columns of green glyphs rain down and take the window with them.
    Matrix,
    /// The Doom screen melt: columns slide down at their own pace.
    Doom,
    /// Twisted into a vortex and pulled away.
    Apparition,
}

impl CloseEffect {
    /// Every effect, in the order the shader numbers them.
    pub const ALL: [CloseEffect; 11] = [
        CloseEffect::Fade,
        CloseEffect::Fire,
        CloseEffect::Incinerate,
        CloseEffect::Tv,
        CloseEffect::Glide,
        CloseEffect::Pixelate,
        CloseEffect::Hexagon,
        CloseEffect::Energize,
        CloseEffect::Matrix,
        CloseEffect::Doom,
        CloseEffect::Apparition,
    ];

    pub fn name(self) -> &'static str {
        match self {
            CloseEffect::Fade => "fade",
            CloseEffect::Fire => "fire",
            CloseEffect::Incinerate => "incinerate",
            CloseEffect::Tv => "tv",
            CloseEffect::Glide => "glide",
            CloseEffect::Pixelate => "pixelate",
            CloseEffect::Hexagon => "hexagon",
            CloseEffect::Energize => "energize",
            CloseEffect::Matrix => "matrix",
            CloseEffect::Doom => "doom",
            CloseEffect::Apparition => "apparition",
        }
    }

    pub fn from_name(name: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|e| e.name() == name)
    }

    /// The number `burn.frag` knows this effect by.
    pub fn shader_id(self) -> f32 {
        Self::ALL.iter().position(|e| *e == self).unwrap_or(0) as f32
    }

    /// How long this effect runs relative to the configured duration: a fire
    /// needs time to climb, a television is off in a blink.
    pub fn duration_factor(self) -> f64 {
        match self {
            CloseEffect::Fire | CloseEffect::Incinerate => 1.5,
            CloseEffect::Matrix | CloseEffect::Energize => 1.3,
            CloseEffect::Doom | CloseEffect::Hexagon => 1.2,
            CloseEffect::Tv => 0.8,
            _ => 1.0,
        }
    }

    /// Logical pixels the effect may draw outside the window's picture.
    pub fn overhang(self) -> f64 {
        match self {
            CloseEffect::Fade => 0.0,
            CloseEffect::Fire | CloseEffect::Incinerate | CloseEffect::Energize => 48.0,
            // The twist swings the corners out past the frame.
            CloseEffect::Apparition => 72.0,
            _ => 8.0,
        }
    }
}

/// What `[effects.close_animation] style` selects.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CloseStyle {
    Effect(CloseEffect),
    /// A different shader effect for every window.
    Random,
}

impl CloseStyle {
    pub fn from_name(name: &str) -> Option<Self> {
        match name {
            "random" => Some(CloseStyle::Random),
            other => CloseEffect::from_name(other).map(CloseStyle::Effect),
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            CloseStyle::Random => "random",
            CloseStyle::Effect(effect) => effect.name(),
        }
    }

    /// Every accepted `style` value, for error messages and the settings app.
    pub fn names() -> Vec<&'static str> {
        CloseEffect::ALL
            .iter()
            .map(|e| e.name())
            .chain(std::iter::once("random"))
            .collect()
    }

    /// The effect a window closing now gets. `roll` is any number that
    /// differs from one close to the next; `Random` never lands on the fade.
    pub fn resolve(self, roll: u32) -> CloseEffect {
        match self {
            CloseStyle::Effect(effect) => effect,
            CloseStyle::Random => {
                let shaders = &CloseEffect::ALL[1..];
                shaders[scramble(roll) as usize % shaders.len()]
            }
        }
    }
}

/// xorshift32; visual variety only.
fn scramble(seed: u32) -> u32 {
    let mut x = seed | 1;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    x
}

/// Two numbers in `[0, 1)` that make one close look unlike the next: where the
/// scorch starts, which column melts first.
pub fn seed(roll: u32) -> [f32; 2] {
    let a = scramble(roll);
    let b = scramble(a);
    let unit = |x: u32| (x >> 8) as f32 / (1u32 << 24) as f32;
    [unit(a), unit(b)]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_effect_round_trips_through_its_name() {
        for effect in CloseEffect::ALL {
            assert_eq!(CloseEffect::from_name(effect.name()), Some(effect));
            assert_eq!(
                CloseStyle::from_name(effect.name()),
                Some(CloseStyle::Effect(effect))
            );
        }
        assert_eq!(CloseStyle::from_name("random"), Some(CloseStyle::Random));
        assert_eq!(CloseStyle::from_name("explode"), None);
        assert_eq!(CloseStyle::names().len(), CloseEffect::ALL.len() + 1);
    }

    #[test]
    fn the_shader_numbers_follow_the_list() {
        assert_eq!(CloseEffect::Fade.shader_id(), 0.0);
        assert_eq!(CloseEffect::Fire.shader_id(), 1.0);
        assert_eq!(CloseEffect::Apparition.shader_id(), 10.0);
    }

    #[test]
    fn random_reaches_every_shader_effect_and_never_the_fade() {
        let mut seen = std::collections::HashSet::new();
        for roll in 0..2000 {
            let effect = CloseStyle::Random.resolve(roll);
            assert_ne!(effect, CloseEffect::Fade);
            seen.insert(effect.name());
        }
        assert_eq!(seen.len(), CloseEffect::ALL.len() - 1);
        assert_eq!(
            CloseStyle::Effect(CloseEffect::Tv).resolve(7),
            CloseEffect::Tv
        );
    }

    #[test]
    fn seeds_stay_in_the_unit_square_and_differ() {
        let mut last = [2.0, 2.0];
        for roll in 0..200 {
            let s = seed(roll * 2 + 1);
            assert!(s.iter().all(|v| (0.0..1.0).contains(v)), "{s:?}");
            assert_ne!(s, last);
            last = s;
        }
    }
}
