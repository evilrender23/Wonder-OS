pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Services.Notifications

// The session's notification daemon (replaces swaync). Every notification is
// kept in `history` until dismissed; `popups` holds the ones still showing as
// toasts. Do-not-disturb suppresses toasts except critical ones.
Singleton {
    id: root

    property list<QtObject> history: []
    property list<QtObject> popups: []
    readonly property int count: history.length
    readonly property bool dnd: Settings.data.doNotDisturb
    property bool serverActive: false

    function _remove(listName, n) {
        root[listName] = root[listName].filter(x => x !== n)
    }

    function dismiss(entry) {
        _remove("popups", entry)
        _remove("history", entry)
        if (entry.notification) entry.notification.dismiss()
        entry.destroy()
    }

    function clearAll() {
        for (const e of history.slice()) dismiss(e)
    }

    function hidePopup(entry) {
        _remove("popups", entry)
    }

    function invoke(entry, action) {
        action.invoke()
        dismiss(entry)
    }

    component Entry: QtObject {
        property var notification
        property string appName
        property string appIcon
        property string summary
        property string body
        property string image
        property int urgency
        property var actions: []
        property date time: new Date()
    }

    Component { id: entryComponent; Entry {} }

    NotificationServer {
        id: server
        keepOnReload: false
        actionsSupported: true
        bodyMarkupSupported: true
        bodyHyperlinksSupported: true
        imageSupported: true
        persistenceSupported: true

        onNotification: n => {
            n.tracked = true
            const e = entryComponent.createObject(root, {
                notification: n, appName: n.appName, appIcon: n.appIcon, summary: n.summary,
                body: n.body, image: n.image, urgency: n.urgency, actions: n.actions
            })
            n.closed.connect(() => {
                root._remove("popups", e)
                root._remove("history", e)
            })
            root.history = [e].concat(root.history).slice(0, 60)
            if (!root.dnd || n.urgency === NotificationUrgency.Critical)
                root.popups = [e].concat(root.popups).slice(0, 4)
        }
        Component.onCompleted: root.serverActive = true
    }
}
