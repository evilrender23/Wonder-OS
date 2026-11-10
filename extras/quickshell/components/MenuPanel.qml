import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// A macOS-style drop-down menu: a BarPanel left-aligned under its title, with
// tight padding and MenuRow / MenuSeparator children stacked with no gaps.
BarPanel {
    contentWidth: 240
    padding: 5
    contentSpacing: 0
    menu: true
}
