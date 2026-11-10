import QtQuick
import ".."
import "../services"
import "../components"

// Progress of what Rabbit is copying, moving or deleting, in the
// middle of the bar. Takes no room while nothing runs.
Item {
    id: root

    readonly property bool shown: RabbitOps.active
    readonly property bool unknown: RabbitOps.progress < 0
    readonly property string amount: RabbitOps.detail !== "" ? RabbitOps.detail
        : unknown ? "" : `${Math.round(Math.min(1, RabbitOps.progress) * 100)} %`

    implicitWidth: shown ? row.implicitWidth + 16 : 0
    implicitHeight: Theme.pillHeight
    width: implicitWidth
    height: implicitHeight
    opacity: shown ? 1 : 0
    visible: opacity > 0
    clip: true

    Behavior on implicitWidth { NumberAnimation { duration: Theme.animMs; easing.type: Easing.OutCubic } }
    Behavior on opacity { NumberAnimation { duration: Theme.animMs } }

    Row {
        id: row
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: RabbitOps.kind === "trash" || RabbitOps.kind === "delete" ? "trash-2"
                : RabbitOps.kind === "move" ? "folder-open" : "copy"
            size: 14
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 200)
            text: RabbitOps.status
            color: Theme.p.text
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
            font.weight: Font.Medium
        }

        Rectangle {
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: 140
            height: 4
            radius: 2
            color: Theme.veil(0.16)
            clip: true

            Rectangle {
                visible: !root.unknown
                width: Math.max(height, track.width * Math.max(0, Math.min(1, RabbitOps.progress)))
                height: parent.height
                radius: 2
                color: Theme.p.accent
                Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            }

            // Amount unknown (archives, a job that hasn't counted yet): a
            // segment that keeps crossing the track.
            Rectangle {
                id: runner
                visible: root.unknown
                width: 44
                height: parent.height
                radius: 2
                color: Theme.p.accent
                NumberAnimation on x {
                    running: root.unknown && root.shown
                    from: -runner.width
                    to: track.width
                    duration: 1100
                    loops: Animation.Infinite
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== ""
            text: root.amount
            color: Theme.p.text2
            font.pixelSize: Theme.smallFontSize
            font.family: Theme.fontFamily
            font.features: { "tnum": 1 }
        }
    }
}
