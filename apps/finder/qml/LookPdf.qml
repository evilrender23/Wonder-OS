import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Pdf

/// Quick Look's pages of a PDF: one under the other, as wide as the window.
Item {
    id: pdf
    property url source
    readonly property int pages: doc.status === PdfDocument.Ready ? doc.pageCount : 0
    /// Not a PDF that can be read (damaged, or locked with a password).
    signal broken()

    PdfDocument {
        id: doc
        source: pdf.source
        onStatusChanged: if (doc.status === PdfDocument.Error) pdf.broken()
    }
    ListView {
        id: view
        anchors.fill: parent
        clip: true
        spacing: 10
        topMargin: 4
        bottomMargin: 4
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 1200
        model: pdf.pages
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: page
            required property int index
            readonly property size points: doc.pagePointSize(index)
            width: view.width
            height: sheet.height
            Rectangle {
                id: sheet
                x: 14
                width: page.width - 28
                height: page.points.width > 0 ? Math.round(width * page.points.height / page.points.width) : width
                color: "white"
                PdfPageImage {
                    anchors.fill: parent
                    document: doc
                    currentFrame: page.index
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: Math.round(sheet.width * Screen.devicePixelRatio)
                }
            }
        }
    }
}
