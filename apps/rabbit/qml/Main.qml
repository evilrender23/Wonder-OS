import QtQuick

/// One application, many windows: each request from App opens another.
QtObject {
    id: root
    property var windows: []
    property Component windowComponent: Component {
        RabbitWindow {}
    }
    property Connections requests: Connections {
        target: App
        function onWindowRequested(url, select) {
            root.open(url, select)
        }
        function onInfoRequested(url) {
            root.open(App.parentUrl(url), [url]).showInfo(url)
        }
        function onLookRequested(urls, source, update) {
            root.showLook(urls, source, update)
        }
    }

    /// Quick Look's one window, made the first time it is asked for.
    property QuickLook look: null
    property Component lookComponent: Component {
        QuickLook {}
    }
    function showLook(urls, source, update) {
        if (!look) {
            if (update)
                return
            look = lookComponent.createObject(null)
        }
        look.request(urls, source, update)
    }
    // Test runs of Quick Look alone (--look with --shot): its window is the picture.
    property Timer lookShot: Timer {
        interval: 2500
        running: App.testing && App.testLook.length > 0
        Component.onCompleted: if (running) root.showLook(App.testLook, null, false)
        onTriggered: App.saveShot(root.look)
    }

    function open(url, select) {
        const window = windowComponent.createObject(null, { startUrl: url, startSelect: select, first: windows.length === 0, cascade: windows.length })
        windows = windows.concat([window])
        window.closing.connect(() => {
            windows = windows.filter(other => other !== window)
            window.destroy()
        })
        return window
    }

    /// "Customize Folder" has one window; asking again points it at the other folder.
    property FolderLook customizer: null
    property Component customizerComponent: Component {
        FolderLook {}
    }
    property Connections customizing: Connections {
        target: App
        function onCustomizeRequested(url) {
            if (!root.customizer)
                root.customizer = root.customizerComponent.createObject(null)
            root.customizer.customize(url)
        }
    }
}
