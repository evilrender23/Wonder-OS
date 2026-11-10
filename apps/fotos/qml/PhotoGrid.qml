import QtQuick
import QtQuick.Controls.Basic
import WonderlandFotos

/// A page of pictures: lines of squares under the heading of their month.
/// Only what is on screen exists, so a library of thousands scrolls freely.
ListView {
    id: grid

    property alias photos: photos
    /// The side a square would like to have; the width decides how many fit.
    property int target: 150
    /// The chosen pictures: {path: true}.
    property var selection: ({})
    readonly property bool selecting: Object.keys(selection).length > 0

    signal opened(int index)
    /// A click on a picture, with the keys held.
    signal picked(int index, string path, int modifiers)
    signal menuRequested(int index, string path)
    signal groupPicked(var paths)
    signal backgroundClicked()

    readonly property int gap: 3
    readonly property int margin: 16
    readonly property int columns: Math.max(2, Math.floor((width - 2 * margin + gap) / (target + gap)))
    readonly property real side: Math.floor((width - 2 * margin - gap * (columns - 1)) / columns)

    function reveal(index) {
        const row = photos.rowOf(index)
        if (row >= 0)
            positionViewAtIndex(row, ListView.Contain)
    }

    clip: true
    topMargin: 2
    bottomMargin: 16
    boundsBehavior: Flickable.StopAtBounds
    // Lines just off screen are kept ready: their pictures are there when scrolled to.
    cacheBuffer: 600
    reuseItems: false
    ScrollBar.vertical: ScrollBar {}

    model: PhotoModel {
        id: photos
        library: App.library
        albums: App.albums
        columns: grid.columns
    }

    MouseArea {
        parent: grid.contentItem
        anchors.fill: parent
        z: -1
        onClicked: grid.backgroundClicked()
    }

    delegate: Item {
        id: line
        required property int index
        required property bool header
        required property string title
        required property string detail
        required property var cells

        width: grid.width
        height: header ? 46 : grid.side + grid.gap

        // The heading of a group; a click chooses all of it.
        Item {
            visible: line.header
            x: grid.margin
            width: parent.width - 2 * grid.margin
            height: parent.height
            Text {
                id: titleText
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                text: line.title
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: titleText.right
                anchors.leftMargin: 10
                anchors.baseline: titleText.baseline
                text: line.detail
                color: Theme.dim
                font.pixelSize: 12
            }
            Text {
                anchors.right: parent.right
                anchors.baseline: titleText.baseline
                text: "Seleccionar"
                color: Theme.tint
                font.pixelSize: 12
                opacity: headArea.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 100 } }
            }
            MouseArea {
                id: headArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: grid.groupPicked(photos.group(line.index))
            }
        }

        Row {
            visible: !line.header
            x: grid.margin
            spacing: grid.gap
            Repeater {
                model: line.header ? [] : line.cells
                delegate: Rectangle {
                    id: cell
                    required property var modelData
                    readonly property bool chosen: grid.selection[modelData.path] === true
                    width: grid.side
                    height: grid.side
                    color: Theme.plate
                    clip: true

                    Image {
                        id: picture
                        anchors.fill: parent
                        anchors.margins: cell.chosen ? 7 : 0
                        source: Theme.thumb(cell.modelData.path, cell.modelData.stamp)
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 140 } }
                        Behavior on anchors.margins { NumberAnimation { duration: 90 } }
                    }
                    Sym {
                        anchors.centerIn: parent
                        visible: picture.status === Image.Error
                        name: cell.modelData.video ? "film" : "image"
                        size: 26
                        color: Theme.faint
                    }

                    // A video says so, and how long it is.
                    Rectangle {
                        visible: cell.modelData.video
                        anchors.left: picture.left
                        anchors.right: picture.right
                        anchors.bottom: picture.bottom
                        height: 30
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#00000000" }
                            GradientStop { position: 1; color: "#99000000" }
                        }
                        Sym {
                            x: 7
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 6
                            name: "play-fill"
                            size: 11
                            color: "#ffffff"
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 7
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 5
                            text: cell.modelData.duration > 0 ? App.time(cell.modelData.duration) : ""
                            color: "#ffffff"
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }
                    }
                    // Loved: a heart, on a shade so it shows on a white sky too.
                    Rectangle {
                        visible: App.albums.revision >= 0 && App.albums.isFavorite(cell.modelData.path)
                        anchors.left: picture.left
                        anchors.top: picture.top
                        anchors.margins: 5
                        width: 20
                        height: 20
                        radius: 10
                        color: "#59000000"
                        Sym {
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: 0.5
                            name: "heart-fill"
                            size: 12
                            color: "#ffffff"
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: cell.chosen ? 3 : 0
                        border.color: Theme.tint
                    }
                    // The tick: shown on what is chosen, and offered under the pointer.
                    Rectangle {
                        id: tick
                        visible: cell.chosen || area.containsMouse
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        width: 20
                        height: 20
                        radius: 10
                        color: cell.chosen ? Theme.tint : "#55000000"
                        border.color: "#ffffff"
                        border.width: 1.5
                        Sym {
                            anchors.centerIn: parent
                            visible: cell.chosen
                            name: "check"
                            size: 12
                            color: "#ffffff"
                        }
                    }

                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) {
                                grid.menuRequested(cell.modelData.index, cell.modelData.path)
                                return
                            }
                            // On the tick, or while choosing, a click chooses; otherwise it opens.
                            const onTick = mouse.x >= tick.x - 4 && mouse.y <= tick.y + tick.height + 4
                            if (onTick || grid.selecting || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                                grid.picked(cell.modelData.index, cell.modelData.path, onTick && !(mouse.modifiers & Qt.ShiftModifier) ? Qt.ControlModifier : mouse.modifiers)
                            else
                                grid.opened(cell.modelData.index)
                        }
                    }
                }
            }
        }
    }
}
