//! Any sequence of IPC requests — hostile numbers (NaN, ±∞, 1e308), ids that
//! don't exist, garbage action strings, requests that only make sense on
//! their own connection — leaves the compositor running with its invariants
//! intact. Each reply may be an error; none may be a panic.
//!
//! The IPC socket is the widest door a local process has into the
//! compositor, so this is the mechanical form of "a bad request never
//! crashes": the fixture checks the stage invariants on every dispatch, and
//! its teardown checks nothing leaked.

use proptest::prelude::*;

use super::{Fixture, map_window};
use crate::ipc::dispatch;
use crate::ipc::protocol::{Request, WindowSelector};

fn hostile_f64() -> impl Strategy<Value = f64> + Clone {
    prop_oneof![
        3 => -5000.0..5000.0f64,
        1 => Just(f64::NAN),
        1 => Just(f64::INFINITY),
        1 => Just(f64::NEG_INFINITY),
        1 => Just(1e308),
        1 => Just(-1e17),
        1 => Just(0.0),
        1 => Just(-0.0),
        1 => Just(f64::MIN_POSITIVE),
        1 => any::<f64>(),
    ]
}

fn hostile_i32() -> impl Strategy<Value = i32> + Clone {
    prop_oneof![
        3 => -4000..4000i32,
        1 => Just(i32::MAX),
        1 => Just(i32::MIN),
        1 => Just(0),
        1 => any::<i32>(),
    ]
}

fn selector() -> impl Strategy<Value = Option<WindowSelector>> {
    prop_oneof![
        1 => Just(None),
        3 => (0u64..12).prop_map(|id| Some(WindowSelector::Id(id))),
        1 => Just(Some(WindowSelector::Id(u64::MAX))),
        2 => prop::sample::select(vec!["a", "b", "c", "nope", ""])
            .prop_map(|s| Some(WindowSelector::AppId(s.to_string()))),
    ]
}

/// Config-grammar action strings: real ones, real ones with bad arguments,
/// and noise. Excludes anything that spawns processes or stops the loop.
fn action() -> impl Strategy<Value = String> {
    prop::sample::select(vec![
        "zoom-in",
        "zoom-out",
        "zoom-reset",
        "zoom-to-fit",
        "zoom-to-fit-snapped",
        "home-toggle",
        "center-window",
        "center-nearest up",
        "focus-nearest left",
        "focus-center",
        "cycle-windows forward",
        "cycle-windows backward",
        "fit-window",
        "fit-window-snapped",
        "fill-window",
        "resize-to-snap",
        "shrink-window",
        "toggle-fullscreen",
        "close-window",
        "set-bookmark x",
        "go-to-bookmark x",
        "go-to-bookmark missing",
        "move-to-bookmark x",
        "switch-layout next",
        "switch-layout 99",
        "pan-viewport 1e308 1e308",
        "pan-viewport nan nan",
        "pan-viewport",
        "go-to-bookmark",
        "zoom-in please",
        "",
        "   ",
        "not-an-action",
        "send-to-output nowhere",
        "send-cursor-to-output nowhere",
    ])
    .prop_map(str::to_string)
}

fn request() -> impl Strategy<Value = Request> {
    let point = (hostile_f64(), hostile_f64());
    let ipoint = (hostile_i32(), hostile_i32());
    prop_oneof![
        prop::option::of(point.clone()).prop_map(Request::Camera),
        prop::option::of(hostile_f64()).prop_map(Request::Zoom),
        any::<bool>().prop_map(|short| Request::Layout { short }),
        Just(Request::Cursor),
        Just(Request::State),
        Just(Request::DebugCounters),
        // Only valid as a connection's own request; through dispatch it must
        // be refused, not hit an unreachable!().
        Just(Request::Subscribe),
        selector().prop_map(Request::Focus),
        (selector(), prop::option::of(ipoint.clone()))
            .prop_map(|(window, to)| Request::Move { window, to }),
        (selector(), prop::option::of(ipoint))
            .prop_map(|(window, to)| Request::Resize { window, to }),
        (selector(), prop::option::of(hostile_f64()))
            .prop_map(|(window, value)| Request::Opacity { window, value }),
        (selector(), prop::option::of(any::<bool>()))
            .prop_map(|(window, value)| Request::Pin { window, value }),
        (selector(), prop::option::of(any::<bool>()))
            .prop_map(|(window, value)| Request::Minimize { window, value }),
        selector().prop_map(Request::Close),
        selector().prop_map(Request::Suspend),
        selector().prop_map(Request::Relaunch),
        action().prop_map(Request::Action),
        (
            prop::option::of(prop::sample::select(vec!["x", "y", "", "1"]).prop_map(String::from)),
            prop::option::of(point),
            any::<bool>(),
        )
            .prop_map(|(name, to, delete)| Request::Bookmark { name, to, delete }),
    ]
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 96, ..ProptestConfig::default() })]

    #[test]
    fn no_request_sequence_panics(requests in prop::collection::vec(request(), 1..40)) {
        let mut f = Fixture::new();
        f.add_output(1, (1280, 800));
        let client = f.add_client();
        for (app_id, size) in [("a", (400, 300)), ("b", (300, 300)), ("c", (640, 480))] {
            map_window(&mut f, client, app_id, size);
        }
        for request in requests {
            // Ok or Err are both fine; the property is that we get here.
            let _ = dispatch(request, f.state());
            f.dispatch();
        }
    }
}
