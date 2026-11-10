import QtQuick
import QtQuick.Controls.Basic

/// The slide in hand on its table: what is on it is picked, moved, sized, turned and typed into here.
Item {
    id: stage
    property var ui
    readonly property var deck: App.deck
    /// The scale at which the whole slide is in sight.
    readonly property real fit: Math.max(0.05, Math.min((width - 72) / deck.width, (height - 72) / deck.height))
    /// Pixels per point of the slide.
    readonly property real s: ui.zoom > 0 ? ui.zoom : fit
    readonly property var items: { deck.revision; deck.current; return deck.items(deck.current) }
    readonly property var chosen: items.filter(item => deck.selection.indexOf(item.id) >= 0)
    readonly property var single: chosen.length === 1 ? chosen[0] : null
    readonly property var box: { deck.revision; deck.selection; return deck.selectionBounds() }
    /// The item whose text is being typed, and what it is like.
    property string editing: ""
    property var edited: ({})
    property bool dragging: false

    function begin(id) {
        const item = deck.item(id)
        if (!item.texty)
            return
        commit()
        const start = deck.textDefaults(id)
        edited = item
        editor.font.family = start.font
        editor.font.pixelSize = Math.max(1, Math.round(start.size))
        editor.font.bold = start.bold
        editor.color = start.color
        editor.horizontalAlignment = [TextEdit.AlignLeft, TextEdit.AlignHCenter, TextEdit.AlignRight, TextEdit.AlignJustify][start.align] || TextEdit.AlignLeft
        editor.text = item.plain === "" ? "" : deck.html(id)
        editing = id
        editor.forceActiveFocus()
        editor.selectAll()
    }
    function commit() {
        if (editing === "")
            return
        const id = editing
        editing = ""
        if (editor.length === 0)
            deck.setPlain(id, "")
        else
            deck.setHtml(id, editor.text)
        stage.forceActiveFocus()
    }
    /// Formats the words selected in the editor; false when there are none, and the whole item is to be formatted.
    function formatWords(key, value) {
        if (editing === "" || editor.selectedText === "")
            return false
        const words = editor.cursorSelection
        if (key === "bold") words.font.bold = value
        else if (key === "italic") words.font.italic = value
        else if (key === "underline") words.font.underline = value
        else if (key === "strike") words.font.strikeout = value
        else if (key === "size") words.font.pixelSize = Math.round(value)
        else if (key === "font") words.font.family = value
        else if (key === "color") words.color = value
        else return false
        return true
    }
    function wordsFormat() {
        return editing !== "" && editor.selectedText !== "" ? editor.cursorSelection.font : null
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.desk
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: Math.max(width, stage.deck.width * stage.s + 72)
        contentHeight: Math.max(height, stage.deck.height * stage.s + 72)
        clip: true
        interactive: false
        ScrollBar.vertical: ScrollBar { width: 9; policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        ScrollBar.horizontal: ScrollBar { height: 9; policy: flick.contentWidth > flick.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

        MouseArea {
            id: pointer
            width: flick.contentWidth
            height: flick.contentHeight
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property point from
            property string mode
            property bool moved: false
            function onSlide(mouse) {
                return Qt.point((mouse.x - sheet.x) / stage.s, (mouse.y - sheet.y) / stage.s)
            }
            onPressed: mouse => {
                stage.commit()
                stage.forceActiveFocus()
                const at = onSlide(mouse)
                const hit = stage.deck.hitTest(at.x, at.y, 4 / stage.s)
                from = Qt.point(mouse.x, mouse.y)
                moved = false
                if (mouse.button === Qt.RightButton) {
                    if (hit !== "" && stage.deck.selection.indexOf(hit) < 0)
                        stage.deck.select(hit)
                    mode = ""
                    menu.popup()
                    return
                }
                if (hit !== "") {
                    if (mouse.modifiers & Qt.ShiftModifier)
                        stage.deck.select(hit, true)
                    else if (stage.deck.selection.indexOf(hit) < 0)
                        stage.deck.select(hit)
                    mode = "move"
                    stage.deck.startDrag()
                } else {
                    if (!(mouse.modifiers & Qt.ShiftModifier))
                        stage.deck.clearSelection()
                    mode = "band"
                }
            }
            onPositionChanged: mouse => {
                if (!pressed || mode === "")
                    return
                const dx = mouse.x - from.x
                const dy = mouse.y - from.y
                if (!moved && Math.abs(dx) + Math.abs(dy) < 4)
                    return
                moved = true
                if (mode === "move") {
                    stage.dragging = true
                    // Alt lets go of the magnet.
                    stage.deck.moveBy(dx / stage.s, dy / stage.s, mouse.modifiers & Qt.AltModifier ? 0 : 6 / stage.s)
                } else {
                    band.x = Math.min(from.x, mouse.x)
                    band.y = Math.min(from.y, mouse.y)
                    band.width = Math.abs(dx)
                    band.height = Math.abs(dy)
                    band.visible = true
                }
            }
            onReleased: mouse => {
                if (mode === "move") {
                    stage.deck.finishDrag("Mover")
                    stage.dragging = false
                } else if (mode === "band" && moved) {
                    stage.deck.selectRect((band.x - sheet.x) / stage.s, (band.y - sheet.y) / stage.s, band.width / stage.s, band.height / stage.s)
                }
                band.visible = false
                mode = ""
            }
            onDoubleClicked: mouse => {
                const at = onSlide(mouse)
                const hit = stage.deck.hitTest(at.x, at.y, 4 / stage.s)
                if (hit !== "") {
                    stage.deck.finishDrag("")
                    mode = ""
                    stage.deck.select(hit)
                    stage.begin(hit)
                }
            }
            onWheel: wheel => {
                if (wheel.modifiers & Qt.ControlModifier) {
                    stage.ui.run(wheel.angleDelta.y > 0 ? "zoomIn" : "zoomOut")
                } else {
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - wheel.angleDelta.y))
                    flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - wheel.angleDelta.x))
                }
            }
        }

        Rectangle {
            // The shadow the slide casts on the table.
            x: sheet.x - 1
            y: sheet.y - 1
            width: sheet.width + 2
            height: sheet.height + 2
            color: Theme.dark ? "#60000000" : "#22000000"
        }
        Rectangle {
            x: sheet.x + 2
            y: sheet.y + sheet.height + 1
            width: sheet.width - 4
            height: 2
            color: Theme.dark ? "#30000000" : "#10000000"
        }
        Item {
            id: sheet
            x: Math.round((flick.contentWidth - width) / 2)
            y: Math.round((flick.contentHeight - height) / 2)
            width: Math.round(stage.deck.width * stage.s)
            height: Math.round(stage.deck.height * stage.s)

            SlideView {
                anchors.fill: parent
                deck: stage.deck
                index: stage.deck.current
                editing: true
                skipText: stage.editing
                theme: stage.ui.tryTheme
            }

            // The words being typed, in place.
            Item {
                visible: stage.editing !== ""
                x: (stage.edited.x || 0) * stage.s
                y: (stage.edited.y || 0) * stage.s
                width: stage.edited.w || 10
                height: stage.edited.h || 10
                scale: stage.s
                transformOrigin: Item.TopLeft
                TextArea {
                    id: editor
                    width: parent.width
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    leftPadding: 8
                    rightPadding: 8
                    bottomPadding: 4
                    topPadding: stage.edited.valign === 1 ? Math.max(4, (parent.height - contentHeight) / 2)
                        : stage.edited.valign === 2 ? Math.max(4, parent.height - contentHeight - 4) : 4
                    selectionColor: Theme.accent
                    selectedTextColor: "#ffffff"
                    selectByMouse: true
                    background: null
                    Keys.onEscapePressed: stage.commit()
                    onActiveFocusChanged: if (!activeFocus && !stage.ui.busy) stage.commit()
                }
            }

            // What is selected, framed.
            Repeater {
                model: stage.chosen
                Item {
                    id: frame
                    required property var modelData
                    readonly property bool alone: stage.single !== null && !modelData.line
                    x: modelData.x * stage.s
                    y: modelData.y * stage.s
                    width: modelData.w * stage.s
                    height: modelData.h * stage.s
                    rotation: modelData.rotation
                    Rectangle {
                        visible: !frame.modelData.line
                        anchors.fill: parent
                        color: "transparent"
                        border.color: stage.editing === frame.modelData.id ? Theme.accent : Theme.guide
                        border.width: 1
                    }
                    // The knob that turns it.
                    Rectangle {
                        visible: frame.alone && stage.editing === "" && !stage.dragging
                        x: parent.width / 2 - 0.5
                        y: -20
                        width: 1
                        height: 20
                        color: Theme.guide
                    }
                    Rectangle {
                        visible: frame.alone && stage.editing === "" && !stage.dragging
                        x: parent.width / 2 - 6
                        y: -30
                        width: 12
                        height: 12
                        radius: 6
                        color: "#ffffff"
                        border.color: Theme.guide
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -5
                            cursorShape: Qt.CrossCursor
                            property real first
                            function angle(mouse) {
                                const at = mapToItem(sheet, mouse.x, mouse.y)
                                const cx = (frame.modelData.x + frame.modelData.w / 2) * stage.s
                                const cy = (frame.modelData.y + frame.modelData.h / 2) * stage.s
                                return Math.atan2(at.y - cy, at.x - cx) * 180 / Math.PI
                            }
                            property real base
                            onPressed: mouse => { first = angle(mouse); base = frame.modelData.rotation; stage.deck.startDrag() }
                            onPositionChanged: mouse => {
                                if (!pressed)
                                    return
                                let turn = angle(mouse) - first
                                // Shift goes by steps of fifteen degrees.
                                if (mouse.modifiers & Qt.ShiftModifier)
                                    turn = Math.round((base + turn) / 15) * 15 - base
                                stage.deck.rotateBy(turn)
                            }
                            onReleased: stage.deck.finishDrag("Girar")
                        }
                    }
                    Repeater {
                        model: frame.alone && stage.editing === "" ? ["nw", "n", "ne", "e", "se", "s", "sw", "w"] : []
                        Rectangle {
                            id: knob
                            required property string modelData
                            visible: !stage.dragging
                            x: (modelData.indexOf("w") >= 0 ? 0 : modelData.indexOf("e") >= 0 ? frame.width : frame.width / 2) - 4.5
                            y: (modelData.indexOf("n") >= 0 ? 0 : modelData.indexOf("s") >= 0 ? frame.height : frame.height / 2) - 4.5
                            width: 9
                            height: 9
                            radius: 2
                            color: "#ffffff"
                            border.color: Theme.guide
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: knob.modelData === "n" || knob.modelData === "s" ? Qt.SizeVerCursor
                                    : knob.modelData === "e" || knob.modelData === "w" ? Qt.SizeHorCursor
                                    : knob.modelData === "nw" || knob.modelData === "se" ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                                property point first
                                onPressed: mouse => { first = mapToItem(sheet, mouse.x, mouse.y); stage.deck.startDrag() }
                                onPositionChanged: mouse => {
                                    if (!pressed)
                                        return
                                    const at = mapToItem(sheet, mouse.x, mouse.y)
                                    stage.deck.resizeBy(knob.modelData, (at.x - first.x) / stage.s, (at.y - first.y) / stage.s, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                }
                                onReleased: stage.deck.finishDrag("Cambiar tamaño")
                            }
                        }
                    }
                }
            }
            // A line has two ends, and each goes where it is taken.
            Repeater {
                model: stage.single !== null && stage.single.line ? [0, 1] : []
                Rectangle {
                    id: tip
                    required property int modelData
                    readonly property bool far: modelData === 1
                    x: ((stage.single.flipH !== far) ? stage.single.x + stage.single.w : stage.single.x) * stage.s - 5
                    y: ((stage.single.flipV !== far) ? stage.single.y + stage.single.h : stage.single.y) * stage.s - 5
                    width: 10
                    height: 10
                    radius: 5
                    color: "#ffffff"
                    border.color: Theme.guide
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.CrossCursor
                        property point first
                        onPressed: mouse => { first = mapToItem(sheet, mouse.x, mouse.y); stage.deck.startDrag() }
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            const at = mapToItem(sheet, mouse.x, mouse.y)
                            stage.deck.moveEnd(tip.modelData, (at.x - first.x) / stage.s, (at.y - first.y) / stage.s)
                        }
                        onReleased: stage.deck.finishDrag("Mover extremo")
                    }
                }
            }
            // Several at once: one box around them all, sized as one.
            Item {
                id: many
                visible: stage.chosen.length > 1 && !stage.dragging
                x: (stage.box.x || 0) * stage.s
                y: (stage.box.y || 0) * stage.s
                width: (stage.box.w || 0) * stage.s
                height: (stage.box.h || 0) * stage.s
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    color: "transparent"
                    border.color: Theme.guide
                    opacity: 0.55
                }
                Repeater {
                    model: ["nw", "ne", "se", "sw"]
                    Rectangle {
                        id: corner
                        required property string modelData
                        x: (modelData.indexOf("w") >= 0 ? -3 : many.width + 3) - 4.5
                        y: (modelData.indexOf("n") >= 0 ? -3 : many.height + 3) - 4.5
                        width: 9
                        height: 9
                        radius: 2
                        color: "#ffffff"
                        border.color: Theme.guide
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: corner.modelData === "nw" || corner.modelData === "se" ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                            property point first
                            onPressed: mouse => { first = mapToItem(sheet, mouse.x, mouse.y); stage.deck.startDrag() }
                            onPositionChanged: mouse => {
                                if (!pressed)
                                    return
                                const at = mapToItem(sheet, mouse.x, mouse.y)
                                stage.deck.resizeBy(corner.modelData, (at.x - first.x) / stage.s, (at.y - first.y) / stage.s, true)
                            }
                            onReleased: stage.deck.finishDrag("Cambiar tamaño")
                        }
                    }
                }
            }
            // Where an edge meets another, and where two gaps are the same.
            Repeater {
                model: stage.deck.guides
                Item {
                    id: guide
                    required property var modelData
                    readonly property bool gap: modelData.gap === true
                    readonly property bool flat: Math.abs(modelData.y1 - modelData.y2) < 0.01
                    x: Math.min(modelData.x1, modelData.x2) * stage.s
                    y: Math.min(modelData.y1, modelData.y2) * stage.s
                    width: Math.max(1, Math.abs(modelData.x2 - modelData.x1) * stage.s)
                    height: Math.max(1, Math.abs(modelData.y2 - modelData.y1) * stage.s)
                    Rectangle {
                        anchors.fill: parent
                        color: guide.gap ? Theme.accent : Theme.guide
                    }
                    // A gap that matches another shows its ends and its length.
                    Rectangle {
                        visible: guide.gap
                        x: guide.flat ? -0.5 : -4
                        y: guide.flat ? -4 : -0.5
                        width: guide.flat ? 1 : 9
                        height: guide.flat ? 9 : 1
                        color: Theme.accent
                    }
                    Rectangle {
                        visible: guide.gap
                        x: guide.flat ? parent.width - 0.5 : -4
                        y: guide.flat ? -4 : parent.height - 0.5
                        width: guide.flat ? 1 : 9
                        height: guide.flat ? 9 : 1
                        color: Theme.accent
                    }
                    Rectangle {
                        visible: guide.gap
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: guide.flat ? -12 : 0
                        anchors.horizontalCenterOffset: guide.flat ? 0 : 20
                        width: gapText.implicitWidth + 8
                        height: 16
                        radius: 4
                        color: Theme.accent
                        Text {
                            id: gapText
                            anchors.centerIn: parent
                            text: Math.round(guide.flat ? Math.abs(guide.modelData.x2 - guide.modelData.x1) : Math.abs(guide.modelData.y2 - guide.modelData.y1))
                            color: "#ffffff"
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        Rectangle {
            id: band
            visible: false
            color: Qt.alpha(Theme.accent, 0.12)
            border.color: Theme.accent
        }

        // The orders most likely wanted, beside what is selected.
        Rectangle {
            id: pill
            readonly property string kind: stage.chosen.length > 1 ? "many" : stage.single === null ? ""
                : stage.single.line ? "line" : stage.single.type === "image" ? "image" : stage.single.type === "table" ? "table" : "text"
            readonly property var orders: ({
                text: ["bold", "italic", "underline", "bullets", "", "smaller", "bigger", "", "textCenter", "", "duplicate", "delete"],
                image: ["front", "back", "", "flipH", "flipV", "", "duplicate", "delete"],
                table: ["front", "back", "", "duplicate", "delete"],
                line: ["lineArrow", "", "front", "back", "", "duplicate", "delete"],
                many: ["alignLeft", "alignCenter", "alignRight", "", "alignTop", "alignMiddle", "alignBottom", "", "distributeH", "distributeV", "", "group", "delete"]
            })
            visible: kind !== "" && !stage.dragging && !pointer.pressed && stage.ui.tryTheme === ""
            readonly property real upper: sheet.y + (stage.box.y || 0) * stage.s
            readonly property real lower: sheet.y + ((stage.box.y || 0) + (stage.box.h || 0)) * stage.s
            x: Math.max(flick.contentX + 8, Math.min(flick.contentX + flick.width - width - 8, sheet.x + ((stage.box.x || 0) + (stage.box.w || 0) / 2) * stage.s - width / 2))
            /// Above the selection or below it: wherever it fits without lying on something else of the slide.
            function place() {
                const above = upper - height - 40
                const below = lower + 14
                const fits = at => at >= flick.contentY + 6 && at + height <= flick.contentY + flick.height - 6
                const free = at => !stage.items.some(item => stage.deck.selection.indexOf(item.id) < 0
                    && sheet.x + item.x * stage.s < x + width && sheet.x + (item.x + item.w) * stage.s > x
                    && sheet.y + item.y * stage.s < at + height && sheet.y + (item.y + item.h) * stage.s > at)
                // Else off the slide altogether, over it or under it.
                for (const at of [above, below, sheet.y - height - 10, sheet.y + sheet.height + 10])
                    if (fits(at) && free(at))
                        return at
                return fits(above) ? above : fits(below) ? below : flick.contentY + 8
            }
            y: place()
            width: tools.width + 12
            height: 36
            radius: 10
            color: Theme.menu
            border.color: Theme.border
            Rectangle {
                z: -1
                anchors.fill: parent
                anchors.topMargin: 3
                anchors.bottomMargin: -3
                radius: 10
                color: "#18000000"
            }
            MouseArea { anchors.fill: parent }
            Row {
                id: tools
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Repeater {
                    model: pill.orders[pill.kind] || []
                    Item {
                        id: tool
                        required property string modelData
                        readonly property var action: stage.ui.action(modelData)
                        width: modelData === "" ? 9 : 30
                        height: 26
                        Rectangle {
                            visible: tool.modelData === ""
                            anchors.centerIn: parent
                            width: 1
                            height: 16
                            color: Theme.border
                        }
                        TButton {
                            visible: tool.modelData !== ""
                            symbol: tool.action ? tool.action.symbol : ""
                            checked: stage.ui.isOn(tool.modelData)
                            tone: tool.modelData === "delete" ? Theme.dim : Theme.text
                            onClicked: stage.ui.run(tool.modelData)
                        }
                    }
                }
            }
        }
    }

    FMenu {
        id: menu
        FMenuItem { text: "Cortar"; enabled: stage.deck.selection.length > 0; onTriggered: stage.ui.run("cut") }
        FMenuItem { text: "Copiar"; enabled: stage.deck.selection.length > 0; onTriggered: stage.ui.run("copy") }
        FMenuItem { text: "Pegar"; onTriggered: stage.ui.run("paste") }
        FMenuItem { text: "Duplicar"; enabled: stage.deck.selection.length > 0; onTriggered: stage.ui.run("duplicate") }
        FMenuItem { text: "Eliminar"; enabled: stage.deck.selection.length > 0; onTriggered: stage.ui.run("delete") }
        FSeparator {}
        FMenuItem { text: "Traer al frente"; enabled: stage.deck.selection.length > 0; onTriggered: stage.ui.run("front") }
        FMenuItem { text: "Enviar al fondo"; enabled: stage.deck.selection.length > 0; onTriggered: stage.ui.run("back") }
        FSeparator {}
        FMenuItem { text: "Agrupar"; enabled: stage.deck.selection.length > 1; onTriggered: stage.ui.run("group") }
        FMenuItem { text: "Desagrupar"; enabled: stage.deck.selection.length > 0; onTriggered: stage.ui.run("ungroup") }
        FSeparator {}
        FMenuItem { text: "Seleccionar todo"; onTriggered: stage.ui.run("selectAll") }
    }
}
