//@ pragma UseQApplication
import QtQuick
import Quickshell
import Quickshell.Io
import "services"
import "bar"
import "panels"
import "spotlight"
import "aquiles"
import "displays"
import "desktop"
import "wallpapers"
import "about"
import "dock"
import "contextmenu"
import "capture"
import "launchpad"

// The top bar on every screen, plus the shell's panels and overlays. `qs ipc call bar <fn>` drives the bar from keybindings.
ShellRoot {
    Desktop {}

    Variants {
        model: Quickshell.screens
        delegate: Bar {}
    }

    Toasts {}

    Spotlight {}

    AquilesPanel {}

    Displays {}

    WallpaperPanel {}

    About {}
    Dock {}
    ContextMenu {}
    CaptureOverlay {}

    Launchpad {}

    IpcHandler {
        target: "bar"

        function toggleTheme(): void {
            Settings.data.themeMode = Settings.data.themeMode === "dark" ? "light" : "dark"
        }
        function setTheme(mode: string): void {
            if (mode === "light" || mode === "dark") Settings.data.themeMode = mode
        }
        function toggleLayoutLock(): void {
            Settings.data.layoutLocked = !Settings.data.layoutLocked
        }
        function resetLayout(): void {
            Settings.setLayout(Settings.defaultLayout)
        }
        function togglePanel(id: string): void {
            Popups.panelRequested(id)
        }
        function setBookmarkStyle(style: string): void {
            if (["default", "numbers", "magic"].includes(style)) Settings.data.bookmarkStyle = style
        }
    }
}
