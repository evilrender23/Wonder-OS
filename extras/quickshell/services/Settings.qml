pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Persistent bar state in $XDG_STATE_HOME/wonderlandwm/bar.json, written
// atomically on every change. Unknown keys in the file are ignored and missing
// ones fall back to the defaults below, so an older file keeps working.
Singleton {
    id: root

    readonly property string stateDir: (Quickshell.env("XDG_STATE_HOME") || (Quickshell.env("HOME") + "/.local/state")) + "/wonderlandwm"
    property alias data: adapter

    // Widget ids per bar section, in order. Unknown ids are skipped at load.
    // macOS menu bar: the ☤ menu and the app menus on the left, status menus,
    // Spotlight, Control Centre and the clock (Notification Centre) on the
    // right. Everything else stays available in the settings panel.
    readonly property var defaultLayout: ({
        left: ["launcher", "appmenu"],
        center: [],
        right: ["tray", "privacy", "network", "bluetooth", "audio", "battery", "capture", "spotlight", "control", "clock"]
    })

    // Every widget the bar knows (Bar.registry keys), for the settings panel.
    readonly property var knownWidgets: ["launcher", "appmenu", "spotlight", "aquiles", "control", "privacy", "capture", "bookmarks", "media", "clock", "weather", "tray", "updates",
        "ai", "notifications", "idle", "cpu", "memory", "network", "bluetooth", "audio", "brightness", "battery",
        "zoom", "windows", "keyboard", "power"]

    function setLayout(layout) {
        adapter.layout = JSON.parse(JSON.stringify(layout))
    }

    function allWidgets() { return knownWidgets }

    function placedWidgets() {
        const l = adapter.layout || defaultLayout
        return [].concat(l.left || [], l.center || [], l.right || [])
    }

    function hiddenWidgets() {
        const placed = placedWidgets()
        return knownWidgets.filter(id => !placed.includes(id))
    }

    function hideWidget(id) {
        const l = JSON.parse(JSON.stringify(adapter.layout || defaultLayout))
        for (const s of ["left", "center", "right"]) l[s] = (l[s] || []).filter(x => x !== id)
        adapter.layout = l
    }

    function showWidget(id) {
        if (placedWidgets().includes(id)) return
        const l = JSON.parse(JSON.stringify(adapter.layout || defaultLayout))
        l.right = [id].concat(l.right || [])
        adapter.layout = l
    }

    // Status menus are icon-only by default, as on macOS; a saved style wins.
    readonly property var defaultWidgetStyles: ({
        network: { compact: true }, bluetooth: { compact: true }, audio: { compact: true }
    })

    function widgetStyle(id) {
        return Object.assign({}, defaultWidgetStyles[id] || {}, adapter.widgetStyles[id] || {})
    }

    function setWidgetStyle(id, style) {
        const all = JSON.parse(JSON.stringify(adapter.widgetStyles))
        all[id] = Object.assign({}, all[id] || {}, style)
        adapter.widgetStyles = all
    }

    Process {
        running: true
        command: ["mkdir", "-p", root.stateDir]
    }

    FileView {
        id: file
        path: root.stateDir + "/bar.json"
        watchChanges: true
        atomicWrites: true
        printErrors: false
        onFileChanged: reload()
        onAdapterUpdated: writeAdapter()
        // A missing file is the first run: write the defaults out.
        onLoadFailed: error => {
            if (error === FileViewError.FileNotFound) writeAdapter()
        }

        JsonAdapter {
            id: adapter

            property string themeMode: "light"
            property bool use24h: true
            property string timezone: ""
            property bool layoutLocked: true
            property bool reduceMotion: false
            property bool doNotDisturb: false
            property string bookmarkStyle: "default"   // default | numbers | magic
            property string mediaStyle: "default"       // default | full
            property string weatherLocation: ""          // "" = geolocate by IP
            property string weatherUnits: "metric"
            property var hiddenTrayItems: []
            property var layout: root.defaultLayout
            property var widgetStyles: ({})              // id -> { fill, border, compact }
            property string wallpaper: ""                // "" = the theme's shader
            property string wallpaperDir: ""             // "" = XDG Pictures
            property string theme: "hermes-light"
            property bool syncCompositorTheme: false
            property bool dockEnabled: true
            property bool dockAutohide: false
            property bool dockAllScreens: true
            property bool dockMinimizeIntoApp: false     // true = no tile per minimized window
            property int dockSize: 48
            property var dockPinned: []                  // desktop entry ids, in order
            property bool dockPinsCustomized: false      // false = the default pins
            // Liquid glass: the bar and the shell's panels turn translucent
            // over the compositor's frost ([effects.glass] bends and lights it).
            property bool glass: true
            property real glassOpacity: 0.6              // 0.3 (clear) – 1 (solid)
            property bool contextMenu: true              // fallback right-click menu
            property var contextMenuIgnoredApps: []      // app_ids that never get it
            property bool desktopIcons: true             // the Desktop folder on the primary screen
            property bool desktopMagnet: false           // off: icons stay exactly where they are dropped; on: they pull into line with their neighbours
            property int desktopIconSize: 56
            // AquilesAI (services/Aquiles.qml)
            property string aquilesBackend: "hermes"     // hermes | claude | codex | agy
            // One object, one write: { hermes: { model, provider }, claude: "haiku", codex, agy,
            // recent: [{ backend, id, provider }] }. A missing entry = the engine's own default.
            property var aquilesModels: ({})
            property string aquilesPermission: "edits"   // CLI engines: read | edits | full
            property string aquilesCwd: ""               // "" = home
            property bool aquilesPinned: false
        }
    }
}
