import QtQuick

Image {
    property string name
    property color color: Theme.text
    property int size: 16
    width: size
    height: size
    sourceSize: Qt.size(size * 2, size * 2)
    source: name === "" ? "" : Theme.symbol(name, color)
}
