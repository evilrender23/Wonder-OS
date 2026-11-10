pragma Singleton
import QtCore
import QtQuick

/// What new windows start from. A folder's own view, icon size and
/// arrangement live with the folder (FolderLayout), not here.
Settings {
    category: "view"
    property string defaultView: "icons"
    property int iconSize: 64
    property bool showHidden: false
    property bool sidebar: true
    property bool pathBar: true
    property bool statusBar: true
    property int windowWidth: 1000
    property int windowHeight: 620
    /// Where the last file was chosen for another app: the next "Open" starts there.
    property string pickFolder
}
