pragma Singleton
import QtQuick

/// A drag that started in one of our own windows: what the drop needs to know
/// that the URLs alone don't say.
QtObject {
    property bool active: false
    property url source
    /// Where the pointer grabbed the dragged icon, from its cell's corner.
    property point hotSpot
    /// File name → offset from the grabbed icon, so a group keeps its shape.
    property var offsets: ({})
}
