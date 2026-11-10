import QtQuick
import QtQuick.Controls.Basic

/// Gallery: the current item large, the folder as a strip below.
FocusScope {
    id: view
    required property var win
    required property DirModel dir
    property int renamingRow: -1
    readonly property var current: dir.currentRow >= 0 ? dir.get(dir.currentRow) : null

    function cleanUp() {}
    function cleanUpBy(key) {}
    function placeAtMenu(name) {}
    onRenamingRowChanged: renamingRow = -1

    focus: true
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left)
            dir.selectOnly(Math.max(0, dir.currentRow - 1))
        else if (event.key === Qt.Key_Right)
            dir.selectOnly(Math.min(dir.count - 1, dir.currentRow + 1))
        else if (!win.handleKey(event))
            return
        event.accepted = true
    }
    Connections {
        target: view.dir
        function onCurrentRowChanged() { strip.positionViewAtIndex(view.dir.currentRow, ListView.Contain) }
        function onCountChanged() {
            if (view.dir.currentRow < 0 && view.dir.count > 0)
                view.dir.selectOnly(0)
        }
    }
    Component.onCompleted: if (dir.currentRow < 0 && dir.count > 0) dir.selectOnly(0)

    Image {
        anchors.fill: parent
        anchors.margins: 24
        anchors.bottomMargin: strip.height + 56
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        sourceSize: Qt.size(1024, 1024)
        // A picture that can't be read, or a file whose preview can't be made, shows its icon instead.
        property string failedFor: ""
        onStatusChanged: if (status === Image.Error && view.current) failedFor = view.current.path
        source: !view.current ? "" : (failedFor === view.current.path ? App.icons + view.current.icon : (view.current.mime.startsWith("image/") ? view.current.url : (view.current.thumb ? "image://thumb/" + view.current.path + "?" + view.current.modified.getTime() : App.icons + view.current.icon)))
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: mouse => {
                view.forceActiveFocus()
                if (mouse.button === Qt.RightButton && view.current)
                    view.win.itemMenu()
            }
            onDoubleClicked: if (view.current) view.win.openRow(view.dir.currentRow)
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: strip.top
        anchors.bottomMargin: 14
        width: parent.width - 48
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideMiddle
        text: view.current ? view.current.name + "  ·  " + view.current.kind + (view.current.isDir ? "" : "  ·  " + view.current.sizeText) : ""
        color: Theme.dim
        font.pixelSize: 12
    }
    ListView {
        id: strip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        height: 68
        orientation: ListView.Horizontal
        clip: true
        spacing: 4
        model: view.dir
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        delegate: Rectangle {
            id: tile
            required property int index
            required property string icon
            required property string path
            required property bool thumb
            required property bool selected
            required property var modified
            width: 60
            height: 60
            radius: 7
            color: selected ? Theme.accent : "transparent"
            Image {
                anchors.fill: parent
                anchors.margins: 5
                fillMode: Image.PreserveAspectFit
                asynchronous: tile.thumb
                sourceSize: Qt.size(128, 128)
                property bool failed: false
                onStatusChanged: if (status === Image.Error) failed = true
                source: tile.thumb && !failed ? "image://thumb/" + tile.path + "?" + tile.modified.getTime() : App.icons + tile.icon
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: mouse => {
                    view.forceActiveFocus()
                    view.dir.selectOnly(tile.index)
                    if (mouse.button === Qt.RightButton)
                        view.win.itemMenu()
                }
                onDoubleClicked: Qt.callLater(view.win.openRow, tile.index)
            }
        }
    }
}
