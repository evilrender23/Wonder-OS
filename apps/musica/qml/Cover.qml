import QtQuick

/// An album's picture, or a quiet plate with a glyph where there is none.
Item {
    id: cover
    /// Any audio file of the album.
    property string path
    /// How round the corners are, in percent of the side (50 is a circle).
    property int corner: 6
    property string symbol: "music"
    readonly property bool empty: picture.status !== Image.Ready || picture.implicitWidth <= 1

    Rectangle {
        anchors.fill: parent
        radius: width * cover.corner / 100
        color: Theme.plate
        visible: cover.empty
        Sym {
            anchors.centerIn: parent
            name: cover.symbol
            size: Math.max(12, Math.round(cover.width * 0.38))
            color: Theme.faint
        }
    }
    Image {
        id: picture
        anchors.fill: parent
        asynchronous: true
        visible: !cover.empty
        // In steps, so a window being resized does not decode each cover at every width.
        readonly property int pixels: Math.min(1024, Math.ceil(Math.max(32, cover.width) * Screen.devicePixelRatio / 64) * 64)
        sourceSize: Qt.size(pixels, pixels)
        source: cover.path === "" ? "" : "image://cover/" + cover.corner + "/" + encodeURIComponent(cover.path)
    }
    Rectangle {
        anchors.fill: parent
        radius: width * cover.corner / 100
        color: "transparent"
        border.color: Theme.dark ? "#1affffff" : "#14000000"
        visible: !cover.empty
    }
}
