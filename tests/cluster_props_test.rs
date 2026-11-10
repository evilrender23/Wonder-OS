//! Properties of implicit clusters (connected components over snap
//! adjacency). Layouts are generated the way snapping produces them: each
//! window either lands flush against an earlier one, `gap` apart with some
//! perpendicular overlap, or somewhere free — so clusters of every shape
//! (chains, L's, grids, singletons) and near-miss corner touches show up.

use std::collections::{HashSet, VecDeque};

use driftwm::layout::cluster::{
    ResizeClassification, Side, adjacent_side, cluster_of, resolve_cluster_shifts,
};
use driftwm::layout::snap::SnapRect;
use proptest::prelude::*;

const GAP: f64 = 8.0;

fn rect(x: f64, y: f64, w: f64, h: f64) -> SnapRect {
    SnapRect {
        x_low: x,
        x_high: x + w,
        y_low: y,
        y_high: y + h,
    }
}

/// (anchor index, side, perpendicular offset fraction, w, h, free x, free y,
/// sub-pixel drift).
type Step = (usize, u8, f64, f64, f64, f64, f64, f64);

fn layout() -> impl Strategy<Value = Vec<SnapRect>> {
    let step = (
        any::<usize>(),
        0u8..5,
        -0.9..0.9f64,
        40.0..900.0f64,
        40.0..700.0f64,
        -20_000.0..20_000.0f64,
        -20_000.0..20_000.0f64,
        prop_oneof![3 => Just(0.0), 1 => -1.5..1.5f64],
    );
    prop::collection::vec(step, 1..24).prop_map(|steps: Vec<Step>| {
        let mut out: Vec<SnapRect> = Vec::new();
        for (anchor, side, off, w, h, fx, fy, drift) in steps {
            if out.is_empty() || side == 4 {
                out.push(rect(fx.round(), fy.round(), w.round(), h.round()));
                continue;
            }
            let a = out[anchor % out.len()];
            let (aw, ah) = (a.x_high - a.x_low, a.y_high - a.y_low);
            let r = match side {
                0 => rect(a.x_high + GAP + drift, a.y_low + off * ah, w, h),
                1 => rect(a.x_low - GAP - w + drift, a.y_low + off * ah, w, h),
                2 => rect(a.x_low + off * aw, a.y_high + GAP + drift, w, h),
                _ => rect(a.x_low + off * aw, a.y_low - GAP - h + drift, w, h),
            };
            out.push(r);
        }
        out
    })
}

fn reference_cluster(root: usize, rects: &[SnapRect]) -> HashSet<usize> {
    let mut seen = HashSet::from([root]);
    let mut queue = VecDeque::from([root]);
    while let Some(i) = queue.pop_front() {
        for (j, r) in rects.iter().enumerate() {
            if !seen.contains(&j) && adjacent_side(&rects[i], r, GAP).is_some() {
                seen.insert(j);
                queue.push_back(j);
            }
        }
    }
    seen
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 512, ..ProptestConfig::default() })]

    #[test]
    fn adjacency_is_symmetric(rects in layout()) {
        for (i, a) in rects.iter().enumerate() {
            for b in &rects[i + 1..] {
                match adjacent_side(a, b, GAP) {
                    Some(side) => prop_assert_eq!(adjacent_side(b, a, GAP), Some(side.opposite())),
                    None => prop_assert_eq!(adjacent_side(b, a, GAP), None),
                }
            }
        }
    }

    #[test]
    fn a_window_is_never_adjacent_to_itself(rects in layout()) {
        for r in &rects {
            prop_assert_eq!(adjacent_side(r, r, GAP), None);
        }
    }

    #[test]
    fn clusters_partition_the_windows(rects in layout()) {
        let windows: Vec<(usize, SnapRect)> = rects.iter().copied().enumerate().collect();
        let mut owner = vec![None; rects.len()];
        for i in 0..rects.len() {
            let c = cluster_of(&i, &windows, GAP);
            prop_assert!(c.contains(&i), "a cluster contains its root");
            // Same set from every member: membership is an equivalence.
            for &m in &c {
                prop_assert_eq!(&cluster_of(&m, &windows, GAP), &c);
                if let Some(prev) = owner[m] {
                    prop_assert_eq!(prev, *c.iter().min().unwrap());
                }
                owner[m] = Some(*c.iter().min().unwrap());
            }
        }
    }

    #[test]
    fn a_cluster_is_exactly_the_adjacency_component(rects in layout(), root in any::<usize>()) {
        let windows: Vec<(usize, SnapRect)> = rects.iter().copied().enumerate().collect();
        let root = root % rects.len();
        prop_assert_eq!(cluster_of(&root, &windows, GAP), reference_cluster(root, &rects));
    }

    #[test]
    fn resize_cascade_terminates_and_rests_at_zero_delta(
        rects in layout(),
        axes in prop::collection::vec((0u8..3, 0u8..3), 24),
        dw in -800..800i32, dh in -800..800i32,
    ) {
        let members: Vec<ResizeClassification> = rects
            .iter()
            .zip(axes)
            .map(|(r, (ax, ay))| ResizeClassification {
                axis_x: [None, Some(Side::Left), Some(Side::Right)][ax as usize],
                axis_y: [None, Some(Side::Top), Some(Side::Bottom)][ay as usize],
                initial_rect: *r,
            })
            .collect();
        // Any delta: it returns (proptest would hang on a cascade that
        // doesn't), and only ever names real members.
        let (shifts, bonds) = resolve_cluster_shifts(&members, dw, dh, GAP, &[], None);
        prop_assert!(shifts.keys().all(|&i| i < members.len()));
        prop_assert!(bonds.iter().all(|&(a, b)| a < members.len() && b < members.len()));
        // No delta, no bonds, no primary: nothing moves.
        let (still, _) = resolve_cluster_shifts(&members, 0, 0, GAP, &[], None);
        prop_assert!(still.values().all(|&d| d == (0, 0)), "{still:?}");
    }
}
