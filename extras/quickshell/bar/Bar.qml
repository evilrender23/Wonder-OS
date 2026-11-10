import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import ".."
import "../services"
import "../components"

// The top bar on one screen: three sections (left, center, right) whose
// widgets come from Settings.data.layout. With the layout unlocked every
// widget shows a grip and can be dragged within or across sections.
PanelWindow {
    id: bar

    required property var modelData
    screen: modelData

    WlrLayershell.namespace: "wonderland-bar"
    WlrLayershell.layer: WlrLayer.Top
    anchors { top: true; left: true; right: true }
    implicitHeight: Theme.barHeight
    exclusiveZone: Theme.barHeight
    color: "transparent"

    readonly property var registry: ({
        launcher: "widgets/LauncherWidget.qml",
        appmenu: "widgets/AppMenuWidget.qml",
        spotlight: "widgets/SpotlightWidget.qml",
        aquiles: "widgets/AquilesWidget.qml",
        control: "widgets/ControlCenterWidget.qml",
        privacy: "widgets/PrivacyWidget.qml",
        capture: "widgets/CaptureWidget.qml",
        bookmarks: "widgets/BookmarksWidget.qml",
        media: "widgets/MediaWidget.qml",
        clock: "widgets/ClockWidget.qml",
        weather: "widgets/WeatherWidget.qml",
        tray: "widgets/TrayWidget.qml",
        updates: "widgets/UpdatesWidget.qml",
        ai: "widgets/AiWidget.qml",
        notifications: "widgets/NotificationsWidget.qml",
        idle: "widgets/IdleWidget.qml",
        cpu: "widgets/CpuWidget.qml",
        memory: "widgets/MemoryWidget.qml",
        network: "widgets/NetworkWidget.qml",
        bluetooth: "widgets/BluetoothWidget.qml",
        audio: "widgets/AudioWidget.qml",
        brightness: "widgets/BrightnessWidget.qml",
        battery: "widgets/BatteryWidget.qml",
        zoom: "widgets/ZoomWidget.qml",
        windows: "widgets/WindowsWidget.qml",
        keyboard: "widgets/KeyboardWidget.qml",
        power: "widgets/PowerWidget.qml"
    })

    function widgetsFor(section) {
        const ids = (Settings.data.layout && Settings.data.layout[section]) || Settings.defaultLayout[section]
        return ids.filter(id => id in registry)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.glassy(Theme.p.sidebar, 1)
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.p.stroke2
        }
    }

    // A click on the bar itself closes the open panel: the bar is the popup's
    // parent, so a grab doesn't dismiss on it, and a panel opened from a
    // shortcut has no grab at all.
    MouseArea {
        anchors.fill: parent
        onPressed: if (Popups.current) Popups.current.close()
    }

    BarSection {
        id: left
        section: "left"
        bar: bar
        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
    }

    // Centred when there's room; otherwise slides away from whichever side
    // section it would overlap (narrow outputs, long media titles).
    // The Finder's file operations come first there, while one runs.
    Row {
        anchors.verticalCenter: parent.verticalCenter
        x: Math.round(Math.max(left.x + left.width + 12,
                               Math.min((bar.width - width) / 2, right.x - width - 12)))
        spacing: fileProgress.width > 0 && center.width > 0 ? 10 : 0

        FileProgress { id: fileProgress }

        BarSection {
            id: center
            section: "center"
            bar: bar
        }
    }

    BarSection {
        id: right
        section: "right"
        bar: bar
        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
    }

    // Keep-awake (Control Centre, or the idle widget) inhibits on the bar's
    // own surface, so it works whichever widgets are placed.
    IdleInhibitor {
        window: bar
        enabled: Session.idleInhibited
    }
}
