import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window

/// "Customize Folder": a colour for the folder and a symbol or an emoji on its
/// front. Every choice is applied at once, as on macOS; there is nothing to save.
Window {
    id: win

    property url folder
    /// color, symbol, emoji, icon, name: what the folder wears now.
    property var look: ({})
    property string tab: "symbol"
    property string problem: ""

    readonly property var colors: ["", "ff453a", "ff9f0a", "ffd60a", "32d74b", "40c8e0", "0a84ff", "bf5af2", "ff6fae", "ac8e68", "98989d"]
    readonly property var colorNames: [qsTr("Default"), qsTr("Red"), qsTr("Orange"), qsTr("Yellow"), qsTr("Green"), qsTr("Teal"), qsTr("Blue"), qsTr("Purple"), qsTr("Pink"), qsTr("Brown"), qsTr("Gray")]

    // Each symbol with the words that find it, in English and Spanish.
    readonly property var symbols: [
        ["star", "favorite estrella favorito"], ["heart", "love corazón amor"], ["bookmark", "marcador"], ["flag", "bandera"],
        ["tag", "label etiqueta"], ["sparkles", "magic brillos magia"], ["zap", "lightning rayo energía"], ["flame", "fire fuego llama"],
        ["house", "home casa inicio"], ["user", "person usuario persona"], ["users", "people team gente equipo"], ["baby", "bebé niño"],
        ["smile", "face cara sonrisa"], ["ghost", "fantasma"], ["skull", "calavera"], ["bot", "robot ai ia"],
        ["file-text", "document documento texto"], ["book", "libro"], ["book-open", "read libro leer"], ["newspaper", "news periódico noticias"],
        ["briefcase", "work trabajo maletín"], ["graduation-cap", "school study escuela estudios universidad"], ["school", "escuela colegio"], ["presentation", "slides presentación"],
        ["calendar", "date calendario fecha"], ["clock", "time reloj hora"], ["bell", "notification campana aviso"], ["mail", "email correo"],
        ["message-circle", "chat mensaje"], ["phone", "call teléfono"], ["printer", "impresora"], ["calculator", "calculadora"],
        ["code", "programming código programación"], ["terminal", "shell consola"], ["braces", "json código"], ["file-code", "source código fuente"],
        ["git-branch", "git rama repo"], ["bug", "error bicho fallo"], ["binary", "bits binario"], ["hash", "number almohadilla"],
        ["at-sign", "arroba"], ["cpu", "chip procesador"], ["database", "data base de datos"], ["server", "servidor"],
        ["hard-drive", "disk disco"], ["usb", "pendrive"], ["cloud", "nube"], ["wifi", "network red"],
        ["globe", "web world mundo internet"], ["link", "enlace"], ["download", "descarga"], ["upload", "subida"],
        ["circle-arrow-down", "downloads descargas"], ["archive", "box archivo"], ["box", "caja"], ["package", "paquete"],
        ["lock", "private candado privado"], ["key", "password llave clave"], ["shield", "security escudo seguridad"], ["eye", "view ojo ver"],
        ["search", "find buscar lupa"], ["settings", "gear ajustes engranaje"], ["wrench", "tool llave herramienta"], ["hammer", "build martillo"],
        ["laptop", "computer portátil ordenador"], ["monitor", "screen pantalla escritorio"], ["smartphone", "mobile móvil teléfono"], ["tv", "tele televisión"],
        ["watch", "reloj"], ["image", "photo picture imagen foto"], ["camera", "photo cámara foto"], ["video", "vídeo"],
        ["film", "movie película cine"], ["clapperboard", "cinema claqueta cine"], ["tv-minimal-play", "series streaming"], ["popcorn", "palomitas cine"],
        ["music", "song música canción"], ["headphones", "audio auriculares"], ["mic", "microphone micrófono podcast"], ["disc", "cd disco"],
        ["guitar", "guitarra"], ["piano", "teclado"], ["drama", "theatre teatro máscaras"], ["palette", "art color paleta arte"],
        ["brush", "paint pincel pintura"], ["pen-tool", "design vector diseño"], ["pencil", "write lápiz escribir"], ["type", "font tipografía letra"],
        ["scissors", "cut tijeras"], ["languages", "translate idiomas traducir"], ["gamepad-2", "games juegos mando"], ["joystick", "arcade juegos"],
        ["dice-5", "dado azar"], ["puzzle", "rompecabezas"], ["swords", "battle espadas rol"], ["trophy", "prize trofeo premio"],
        ["crown", "king corona"], ["gem", "diamond gema diamante"], ["gift", "present regalo"], ["shopping-cart", "carrito compras"],
        ["shopping-bag", "bolsa compras"], ["wallet", "cartera"], ["credit-card", "tarjeta pago"], ["banknote", "money dinero billete"],
        ["piggy-bank", "savings hucha ahorros"], ["receipt", "invoice recibo factura"], ["chart-column", "stats gráfico estadísticas"], ["chart-pie", "gráfico tarta"],
        ["landmark", "bank banco gobierno"], ["building-2", "office edificio oficina empresa"], ["scale", "law balanza legal"], ["gavel", "juez mazo legal"],
        ["hospital", "health salud"], ["stethoscope", "doctor médico salud"], ["pill", "medicine pastilla medicina"], ["cross", "cruz"],
        ["dumbbell", "gym fitness pesas gimnasio"], ["bike", "bici bicicleta"], ["footprints", "walk huellas pasos"], ["flask-conical", "science lab matraz ciencia"],
        ["atom", "physics átomo física"], ["dna", "biology adn biología"], ["telescope", "astronomy telescopio"], ["lightbulb", "idea bombilla"],
        ["rocket", "launch cohete"], ["plane", "travel flight avión viaje"], ["car", "coche auto"], ["bus", "autobús"],
        ["train-front", "tren"], ["ship", "boat barco"], ["anchor", "ancla"], ["map", "mapa"],
        ["map-pin", "place location lugar ubicación"], ["compass", "brújula"], ["luggage", "travel maleta viaje"], ["backpack", "mochila"],
        ["tent", "camping tienda acampada"], ["mountain", "montaña"], ["tree-pine", "forest árbol pino bosque"], ["leaf", "nature hoja naturaleza"],
        ["flower-2", "flor"], ["sun", "sol"], ["moon", "night luna noche"], ["snowflake", "winter nieve invierno"],
        ["umbrella", "rain paraguas lluvia"], ["paw-print", "pet huella mascota"], ["dog", "perro"], ["cat", "gato"],
        ["bird", "pájaro"], ["fish", "pez"], ["coffee", "café"], ["utensils", "food restaurant comida cubiertos"],
        ["pizza", "comida"], ["apple", "fruit manzana fruta"], ["cherry", "cereza"], ["cake-slice", "tarta pastel"],
        ["cookie", "galleta"], ["wine", "vino copa"], ["beer", "cerveza"], ["shirt", "clothes camiseta ropa"],
        ["glasses", "gafas"], ["trash-2", "papelera basura"], ["wand-sparkles", "magic varita magia"], ["infinity", "infinito"]
    ]
    readonly property var emoji: "😀 😂 🥹 😊 😍 😎 🤓 🥳 🤔 😴 😡 😱 🤖 👻 💀 👽 👾 🎃 💩 🤡 👶 🧑‍💻 🧑‍🎨 🧑‍🔬 🧑‍🍳 🧑‍🎓 👨‍👩‍👧 👀 🧠 👍 👎 👏 🙏 💪 ✌️ 👋 ❤️ 🧡 💛 💚 💙 💜 🖤 🤍 💔 💯 ⭐ 🌟 ✨ ⚡ 🔥 💧 🌈 ☀️ 🌙 ☁️ ❄️ 🌍 🌋 🏔️ 🏝️ 🌊 🌲 🌴 🌵 🌸 🌹 🌻 🍀 🍁 🍄 🐶 🐱 🐭 🐰 🦊 🐻 🐼 🐨 🐯 🦁 🐮 🐷 🐸 🐵 🐔 🐧 🐦 🦉 🦄 🐝 🦋 🐢 🐍 🐙 🐬 🐳 🦈 🐉 🦖 🍎 🍊 🍋 🍌 🍉 🍇 🍓 🍒 🍑 🥑 🌽 🥕 🍞 🧀 🍔 🍟 🍕 🌮 🍣 🍜 🍰 🍩 🍪 🍫 🍿 ☕ 🍺 🍷 🍸 ⚽ 🏀 🏈 ⚾ 🎾 🏐 🏓 🥊 🚴 🏊 🏋️ 🧘 🎮 🕹️ 🎲 🧩 ♟️ 🎯 🎳 🎨 🎬 🎤 🎧 🎵 🎸 🎹 🥁 🎺 🎻 📷 🎥 📺 📻 💻 🖥️ ⌨️ 🖱️ 📱 ☎️ 💾 💿 🔋 🔌 💡 🔦 🔧 🔨 ⚙️ 🧰 🧲 🧪 🔬 🔭 💊 🩺 📚 📖 📝 ✏️ 🖌️ 📎 📌 📁 🗂️ 📅 📈 📊 📦 ✉️ 🔒 🔑 🗝️ 💰 💵 💳 💎 🛒 🎁 🎉 🎈 🏆 🥇 🏠 🏢 🏫 🏥 🏦 ⛪ 🏰 ⛺ 🚗 🚕 🚌 🚲 🏍️ 🚂 ✈️ 🚀 🛸 ⛵ ⚓ 🗺️ 🧭 ⏰ ⏳ 🧸 👕 👟 👓 🎒 👑 💄 🚧 ⚠️ ⛔ ✅ ❌ ❓ ❗ ♻️ 🔴 🟠 🟡 🟢 🔵 🟣 ⚫ ⚪ 🏁 🚩 🏳️‍🌈 🏴‍☠️ 🇪🇸 🇲🇽 🇦🇷 🇺🇸 🇯🇵".split(" ")

    readonly property var shownSymbols: {
        const words = search.text.trim().toLowerCase().split(/\s+/).filter(word => word !== "")
        return symbols.filter(entry => words.every(word => (entry[0] + " " + entry[1]).includes(word)))
    }

    /// What a swatch shows: its own colour, or the one plain folders take.
    function swatchColor(hex) {
        if (hex !== "")
            return "#" + hex
        return App.folderTint !== "" ? App.folderTint : "#3fa4f1"
    }

    function customize(url) {
        folder = url
        problem = ""
        search.text = ""
        reload()
        tab = look.emoji ? "emoji" : "symbol"
        show()
        raise()
        requestActivate()
    }
    function reload() {
        look = App.folderLook(folder)
    }
    function apply(color, symbol, emoji) {
        problem = App.setFolderLook(folder, color, symbol, emoji) ? "" : qsTr("This folder’s look can’t be saved here.")
        reload()
    }
    /// Picking what the folder already wears takes it off again.
    function pickSymbol(name) {
        apply(look.color, look.symbol === name ? "" : name, "")
    }
    function pickEmoji(what) {
        apply(look.color, "", look.emoji === what ? "" : what)
    }

    width: 388
    height: 595
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    flags: Qt.Dialog
    title: qsTr("Customize Folder")
    color: Theme.menu

    Shortcut {
        sequences: [StandardKey.Cancel, StandardKey.Close]
        onActivated: win.close()
    }

    component Pill: Rectangle {
        id: pill
        property string text
        property bool primary: false
        property bool on: false
        signal clicked()
        width: Math.max(86, pillText.implicitWidth + 28)
        height: 28
        radius: 7
        color: primary ? Theme.accent : (on ? Theme.sideSelection : (pillArea.containsMouse ? Theme.hover : Theme.field))
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.text
            color: pill.primary ? "white" : Theme.text
            font.pixelSize: 13
        }
        MouseArea {
            id: pillArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: pill.clicked()
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 112
            height: 112
            sourceSize: Qt.size(256, 256)
            source: win.look.icon ? App.icons + win.look.icon : ""
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: win.problem !== "" ? win.problem : (win.look.name ?? "")
            elide: Text.ElideMiddle
            color: win.problem !== "" ? "#ff453a" : Theme.text
            font.pixelSize: 13
            font.bold: win.problem === ""
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Repeater {
                model: win.colors
                Rectangle {
                    id: swatch
                    required property string modelData
                    required property int index
                    readonly property bool on: (win.look.color ?? "") === modelData
                    width: 22
                    height: 22
                    radius: 11
                    color: "transparent"
                    border.width: on ? 2 : 0
                    border.color: Theme.text
                    Rectangle {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        radius: 8
                        color: win.swatchColor(swatch.modelData)
                    }
                    MouseArea {
                        id: swatchArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.apply(swatch.modelData, win.look.symbol, win.look.emoji)
                    }
                    ToolTip.visible: swatchArea.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: win.colorNames[index]
                }
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6
            Pill {
                text: qsTr("Symbol")
                on: win.tab === "symbol"
                onClicked: win.tab = "symbol"
            }
            Pill {
                text: qsTr("Emoji")
                on: win.tab === "emoji"
                onClicked: win.tab = "emoji"
            }
        }

        Rectangle {
            width: parent.width
            height: 28
            radius: 7
            color: Theme.field
            Sym {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                size: 13
                name: win.tab === "symbol" ? "search" : "smile"
                color: Theme.dim
            }
            // Symbols are searched; an emoji missing from the grid can be typed or pasted.
            TextInput {
                id: search
                visible: win.tab === "symbol"
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: Theme.text
                selectionColor: Theme.accent
                font.pixelSize: 13
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: search.text === ""
                    text: qsTr("Search symbols")
                    color: Theme.faint
                    font.pixelSize: 13
                }
            }
            TextInput {
                id: typed
                visible: win.tab === "emoji"
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: Theme.text
                selectionColor: Theme.accent
                font.pixelSize: 13
                onAccepted: {
                    if (text.trim() !== "")
                        win.apply(win.look.color, "", text.trim())
                    text = ""
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: typed.text === ""
                    text: qsTr("Type or paste an emoji, then Enter")
                    color: Theme.faint
                    font.pixelSize: 13
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 243
            radius: 9
            color: Theme.field
            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                cellWidth: Math.floor(width / 8)
                cellHeight: cellWidth
                boundsBehavior: Flickable.StopAtBounds
                model: win.tab === "symbol" ? win.shownSymbols : win.emoji
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    id: cell
                    required property var modelData
                    readonly property string what: win.tab === "symbol" ? modelData[0] : modelData
                    readonly property bool on: win.tab === "symbol" ? win.look.symbol === what : win.look.emoji === what
                    width: grid.cellWidth
                    height: grid.cellHeight
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 7
                        color: cell.on ? Theme.accent : (cellArea.containsMouse ? Theme.hover : "transparent")
                    }
                    Sym {
                        visible: win.tab === "symbol"
                        anchors.centerIn: parent
                        size: 20
                        name: win.tab === "symbol" ? cell.what : ""
                        color: cell.on ? "#ffffff" : Theme.text
                    }
                    Text {
                        visible: win.tab === "emoji"
                        anchors.centerIn: parent
                        text: win.tab === "emoji" ? cell.what : ""
                        font.family: "Noto Color Emoji"
                        font.pixelSize: 21
                    }
                    MouseArea {
                        id: cellArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.tab === "symbol" ? win.pickSymbol(cell.what) : win.pickEmoji(cell.what)
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: qsTr("No symbol matches")
                color: Theme.dim
                font.pixelSize: 13
            }
        }

        Item {
            width: parent.width
            height: 28
            Pill {
                text: qsTr("Reset")
                opacity: win.look.color || win.look.symbol || win.look.emoji ? 1 : 0.45
                onClicked: win.apply("", "", "")
            }
            Pill {
                anchors.right: parent.right
                text: qsTr("Done")
                primary: true
                onClicked: win.close()
            }
        }
    }

    // --- Test runs: --customize with --shot. "--do" takes tab:…, color:…, symbol:…, emoji:…, search:….
    Timer {
        interval: 600
        running: App.testing && win.visible && App.testCustomize.toString() !== ""
        onTriggered: {
            for (const action of App.testActions) {
                const at = action.indexOf(":")
                const value = action.slice(at + 1)
                switch (action.slice(0, at).trim()) {
                case "tab": win.tab = value; break
                case "color": win.apply(value, win.look.symbol, win.look.emoji); break
                case "symbol": win.pickSymbol(value); break
                case "emoji": win.pickEmoji(value); break
                case "search": search.text = value; break
                }
            }
            shot.start()
        }
    }
    Timer {
        id: shot
        interval: 1200
        onTriggered: App.saveShot(win)
    }
}
