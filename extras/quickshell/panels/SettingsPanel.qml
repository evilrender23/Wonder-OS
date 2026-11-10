import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import ".."
import "../services"
import "../components"

// Shell settings: the theme picker (bar palette and, optionally, the
// compositor theme and wallpaper), layout lock/reset, per-widget styles and
// the motion switch. Mirrors Rise's settings menu on the canvas.
BarPanel {
    id: root

    title: "Settings"
    icon: "settings"
    contentWidth: 340

    readonly property var themes: [
        { id: "hermes-light", label: "Hermes light", mode: "light", swatch: ["#f8f9fe", "#0053fd", "#242528"] },
        { id: "hermes-dark", label: "Hermes dark", mode: "dark", swatch: ["#0d1117", "#4a84fe", "#e6edf3"] }
    ]
    property string widgetToStyle: "clock"

    property var wallpapers: []
    property string themeError: ""

    function runTheme() {
        // install.sh puts it in ~/.config/driftwm/scripts (PATH from a display
        // manager often lacks ~/.local/bin); fall back to PATH.
        const cmd = ["sh", "-c", 't="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm/scripts/wonderland-theme"; [ -x "$t" ] || t=wonderland-theme; exec "$t" "$@"', "sh", Settings.data.theme]
        if (Settings.data.wallpaper) cmd.push("--wallpaper", Settings.data.wallpaper)
        themeError = ""
        themeProc.command = cmd
        themeProc.running = true
    }

    function applyTheme(t) {
        Settings.data.theme = t.id
        Settings.data.themeMode = t.mode
        if (Settings.data.syncCompositorTheme) runTheme()
    }

    function setWallpaper(path) {
        Settings.data.wallpaper = path
        runTheme()
    }

    onVisibleChanged: if (visible && !lister.running) lister.running = true

    // wonderland-theme (extras/scripts) rewrites the compositor's
    // decorations/background block; driftwm hot-reloads it.
    Process {
        id: themeProc
        stderr: StdioCollector { id: themeErr }
        onExited: code => { if (code !== 0) root.themeError = themeErr.text.trim().split("\n").slice(-3).join("\n") || "wonderland-theme isn't installed" }
    }

    Process {
        id: lister
        command: ["sh", "-c", 'd=$1; [ -n "$d" ] || d=$(xdg-user-dir PICTURES 2>/dev/null || echo "$HOME/Pictures"); case $d in "~"*) d=$HOME${d#?};; esac; find -L "$d" -maxdepth 2 -type f \\( -iname "*.jpg" -o -iname "*.jpeg" -o -iname "*.png" -o -iname "*.webp" \\) 2>/dev/null | sort | head -n 80', "sh", Settings.data.wallpaperDir]
        stdout: StdioCollector { onStreamFinished: root.wallpapers = text.trim() ? text.trim().split("\n") : [] }
    }

    Label { role: "section"; text: "Theme" }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            model: root.themes
            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property bool current: Settings.data.theme === modelData.id
                Layout.fillWidth: true
                implicitHeight: 70
                radius: Theme.popupRadius
                color: modelData.swatch[0]
                border.width: current ? 2 : 1
                border.color: current ? Theme.p.accent : Theme.p.stroke2
                scale: cardMouse.pressed ? 0.97 : 1
                Behavior on scale { NumberAnimation { duration: Theme.animMs } }

                Row {
                    x: 10; y: 10
                    spacing: 4
                    Repeater {
                        model: card.modelData.swatch.slice(1)
                        delegate: Rectangle { required property string modelData; width: 14; height: 14; radius: 7; color: modelData }
                    }
                }
                Text {
                    x: 10
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    text: card.modelData.label
                    color: card.modelData.swatch[2]
                    font.pixelSize: Theme.fontSize
                    font.family: Theme.fontFamily
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.applyTheme(card.modelData)
                }
            }
        }
    }
    Label { role: "caption"; text: "Background" }
    ListView {
        id: carousel
        Layout.fillWidth: true
        implicitHeight: 64
        orientation: ListView.Horizontal
        spacing: 6
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: [""].concat(root.wallpapers)
        delegate: Rectangle {
            id: thumb
            required property string modelData
            readonly property bool current: Settings.data.wallpaper === modelData
            width: 96
            height: 60
            radius: Theme.radius
            color: Theme.p.track
            border.width: current ? 2 : 1
            border.color: current ? Theme.p.accent : Theme.p.stroke2
            clip: true
            Image {
                anchors.fill: parent
                anchors.margins: thumb.border.width
                visible: thumb.modelData !== ""
                source: thumb.modelData ? "file://" + thumb.modelData : ""
                sourceSize: Qt.size(192, 120)
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
            }
            Column {
                visible: thumb.modelData === ""
                anchors.centerIn: parent
                spacing: 2
                Icon { name: "sparkles"; color: Theme.p.accent; anchors.horizontalCenter: parent.horizontalCenter }
                Label { role: "caption"; text: "Theme"; anchors.horizontalCenter: parent.horizontalCenter }
            }
            MouseArea {
                id: thumbMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.setWallpaper(thumb.modelData)
            }
            Tooltip { target: thumb; text: thumb.modelData ? thumb.modelData.split("/").pop() : "The theme's own canvas shader"; shown: thumbMouse.containsMouse }
        }
        WheelHandler { onWheel: event => carousel.flick(event.angleDelta.y * 8, 0) }
    }
    Label {
        visible: root.wallpapers.length === 0
        role: "caption"
        text: "No images in your Pictures folder (set wallpaperDir in bar.json)."
    }
    Label { visible: root.themeError !== ""; role: "caption"; color: Theme.p.red; text: root.themeError; Layout.fillWidth: true; wrapMode: Text.Wrap }

    Switch {
        text: "Follow with the compositor"
        detail: "Runs wonderland-theme to swap decorations and background"
        checked: Settings.data.syncCompositorTheme
        onToggled: v => Settings.data.syncCompositorTheme = v
    }

    Divider {}
    Label { role: "section"; text: "Bar" }
    Switch {
        text: "Edit layout"
        detail: "Drag widgets between sections, hide them with ×"
        checked: !Settings.data.layoutLocked
        onToggled: v => Settings.data.layoutLocked = !v
    }
    RowLayout {
        Layout.fillWidth: true
        visible: !Settings.data.layoutLocked
        Label { role: "caption"; text: "Hidden:"; visible: hidden.count > 0 }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                id: hidden
                model: Settings.hiddenWidgets()
                delegate: IconButton {
                    required property string modelData
                    text: `+ ${modelData}`
                    size: 24
                    tooltip: "Add back to the right section"
                    onClicked: Settings.showWidget(modelData)
                }
            }
        }
        IconButton { icon: "rotate-ccw"; size: 24; tooltip: "Reset layout"; onClicked: Settings.setLayout(Settings.defaultLayout) }
    }
    Switch {
        text: "Reduce motion"
        checked: Settings.data.reduceMotion
        onToggled: v => Settings.data.reduceMotion = v
    }
    Switch {
        text: "Context menu on right-click"
        checked: Settings.data.contextMenu
        onToggled: v => Settings.data.contextMenu = v
    }

    Divider {}
    Label { role: "section"; text: "Dock" }
    Switch {
        text: "Show the dock"
        checked: Settings.data.dockEnabled
        onToggled: v => Settings.data.dockEnabled = v
    }
    Switch {
        visible: Settings.data.dockEnabled
        text: "Hide automatically"
        detail: "Slides away; point at the bottom edge to bring it back"
        checked: Settings.data.dockAutohide
        onToggled: v => Settings.data.dockAutohide = v
    }
    Switch {
        visible: Settings.data.dockEnabled
        text: "Minimize windows into application icon"
        detail: "Off: each minimized window gets its own tile next to the trash"
        checked: Settings.data.dockMinimizeIntoApp
        onToggled: v => Settings.data.dockMinimizeIntoApp = v
    }
    Switch {
        visible: Settings.data.dockEnabled
        text: "On every screen"
        detail: "Off: only on the screen at the layout origin"
        checked: Settings.data.dockAllScreens
        onToggled: v => Settings.data.dockAllScreens = v
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Settings.data.dockEnabled
        Label { role: "caption"; text: "Size" }
        Item { Layout.fillWidth: true }
        Repeater {
            model: [[40, "S"], [48, "M"], [60, "L"], [72, "XL"]]
            delegate: IconButton {
                required property var modelData
                text: modelData[1]
                size: 24
                tooltip: `${modelData[0]} px`
                checked: Settings.data.dockSize === modelData[0]
                onClicked: Settings.data.dockSize = modelData[0]
            }
        }
    }

    Divider {}
    Label { role: "section"; text: "Glass" }
    Switch {
        text: "Liquid glass"
        detail: "Bar, Spotlight and panels turn translucent over the compositor's frost"
        checked: Settings.data.glass
        onToggled: v => Settings.data.glass = v
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Settings.data.glass
        Label { role: "caption"; text: "Clear" }
        ThinSlider {
            id: glassSlider
            Layout.fillWidth: true
            from: 0.3; to: 1; stepSize: 0.05
            onMoved: Settings.data.glassOpacity = value
            Binding on value { when: !glassSlider.pressed; value: Settings.data.glassOpacity }
        }
        Label { role: "caption"; text: "Solid" }
    }

    Divider {}
    Label { role: "section"; text: "Desktop" }
    Switch {
        text: "Desktop icons"
        detail: "The Desktop folder on the primary screen, placed freely"
        checked: Settings.data.desktopIcons
        onToggled: v => Settings.data.desktopIcons = v
    }
    Switch {
        text: "Magnetic alignment"
        detail: "Off, icons stay exactly where you drop them. On, dragged icons pull into line with their neighbours; hold Shift to place one freely"
        checked: Settings.data.desktopMagnet
        onToggled: v => Settings.data.desktopMagnet = v
    }

    Divider {}
    Label { role: "section"; text: "Widget style" }
    RowLayout {
        Layout.fillWidth: true
        Label { role: "caption"; text: "Widget" }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: Settings.allWidgets()
                delegate: IconButton {
                    required property string modelData
                    text: modelData
                    size: 22
                    checked: root.widgetToStyle === modelData
                    onClicked: root.widgetToStyle = modelData
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        readonly property var st: Settings.widgetStyle(root.widgetToStyle)
        Label { role: "caption"; text: "Fill"; Layout.fillWidth: true }
        Repeater {
            model: ["none", "tint", "solid"]
            delegate: IconButton {
                required property string modelData
                text: modelData
                size: 22
                checked: (parent.st.fill || "none") === modelData
                onClicked: Settings.setWidgetStyle(root.widgetToStyle, { fill: modelData })
            }
        }
        IconButton {
            text: "border"; size: 22
            checked: parent.st.border === true
            onClicked: Settings.setWidgetStyle(root.widgetToStyle, { border: !(parent.st.border === true) })
        }
        IconButton {
            text: "compact"; size: 22
            checked: parent.st.compact === true
            onClicked: Settings.setWidgetStyle(root.widgetToStyle, { compact: !(parent.st.compact === true) })
        }
    }
}
