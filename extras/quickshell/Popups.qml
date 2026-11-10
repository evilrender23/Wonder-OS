pragma Singleton
import Quickshell

// One popover at a time: opening one closes whichever is open.
Singleton {
    property var current: null
    // `qs ipc call bar togglePanel <widget id>`: the widget on the first
    // screen toggles its panel.
    signal panelRequested(string id)
    // Spotlight and the display settings are their own surfaces, not bar
    // popovers: anything can ask for them (the bar's mark, Spotlight itself).
    signal spotlightRequested()
    signal displaysRequested()
    signal wallpapersRequested()
    signal aboutRequested()
    signal aquilesRequested()
    signal launchpadRequested()
}
