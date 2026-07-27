import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    // Typed replica — auto-synced properties and callable slots.
    readonly property var backend: logos.module("chess_ui")
    property bool ready: false

    // --- backend state (auto-synced PROPs, defensively coerced) ---
    readonly property string fen: ready && backend && backend.fen ? backend.fen : ""
    readonly property string legalMoves: ready && backend && backend.legalMoves ? backend.legalMoves : ""
    readonly property string gameState: ready && backend && backend.gameState ? backend.gameState : "starting"
    readonly property string statusText: ready && backend && backend.status ? backend.status : ""
    readonly property string lastMove: ready && backend && backend.lastMove ? backend.lastMove : ""
    readonly property string moveHistory: ready && backend && backend.moveHistory ? backend.moveHistory : ""
    readonly property string evalText: ready && backend && backend.evalText ? backend.evalText : ""
    readonly property string engineName: ready && backend && backend.engineName ? backend.engineName : ""
    readonly property bool playerIsWhite: ready && backend ? backend.playerIsWhite === true : true
    readonly property bool inCheck: ready && backend ? backend.inCheck === true : false

    // --- local UI state ---
    property bool flipped: false
    property string selected: ""
    property var targetMap: ({})
    property string hintMove: ""
    property bool hintPending: false
    property string promoFrom: ""
    property string promoTo: ""

    // --- derived ---
    readonly property bool whiteAtBottom: playerIsWhite !== flipped
    readonly property var pieces: parseFen(fen)
    readonly property string sideToMove: fen.split(" ").length > 1 ? fen.split(" ")[1] : "w"
    readonly property var legalList: legalMoves.length ? legalMoves.split(" ") : []
    readonly property bool myTurn: gameState === "playerTurn" && !hintPending
    readonly property string checkSquare: {
        if (!inCheck)
            return ""
        var king = sideToMove === "w" ? "K" : "k"
        for (var s in pieces)
            if (pieces[s] === king)
                return s
        return ""
    }

    onFenChanged: {
        selected = ""
        targetMap = {}
        hintMove = ""
        hintPending = false
    }
    onGameStateChanged: {
        if (gameState !== "playerTurn") {
            hintPending = false
            selected = ""
        }
        // A new game from the start position produces no fenChanged, so the
        // fen handler alone would leave a stale hint highlight.
        if (gameState === "starting")
            hintMove = ""
    }
    onLegalMovesChanged: targetMap = selected ? targetsFor(selected) : {}
    onSelectedChanged: targetMap = selected ? targetsFor(selected) : {}

    // --- helpers ---
    function parseFen(f) {
        var map = {}
        if (!f)
            return map
        var ranks = f.split(" ")[0].split("/")
        for (var r = 0; r < ranks.length && r < 8; r++) {
            var file = 0
            for (var i = 0; i < ranks[r].length; i++) {
                var c = ranks[r][i]
                if (c >= '1' && c <= '8')
                    file += parseInt(c)
                else {
                    map["abcdefgh"[file] + (8 - r)] = c
                    file++
                }
            }
        }
        return map
    }

    function glyph(p) {
        var g = { "k": "♚", "q": "♛", "r": "♜",
                  "b": "♝", "n": "♞", "p": "♟" }
        return p ? g[p.toLowerCase()] : ""
    }

    function isWhitePiece(p) { return p !== "" && p === p.toUpperCase() }
    function isPlayers(p) { return p !== "" && (isWhitePiece(p) === playerIsWhite) }

    function squareAt(idx) {
        var row = Math.floor(idx / 8)
        var col = idx % 8
        var file = whiteAtBottom ? col : 7 - col
        var rank = whiteAtBottom ? 8 - row : row + 1
        return "abcdefgh"[file] + rank
    }

    function targetsFor(sq) {
        var t = {}
        for (var i = 0; i < legalList.length; i++)
            if (legalList[i].substring(0, 2) === sq)
                t[legalList[i].substring(2, 4)] = true
        return t
    }

    function clickSquare(sq) {
        if (!myTurn)
            return
        var pc = pieces[sq] || ""
        if (selected === "") {
            if (isPlayers(pc))
                selected = sq
            return
        }
        if (sq === selected) {
            selected = ""
            return
        }
        if (isPlayers(pc)) {
            selected = sq
            return
        }
        var mv = selected + sq
        if (legalList.indexOf(mv) !== -1) {
            backend.playerMove(mv)
            selected = ""
        } else if (legalList.indexOf(mv + "q") !== -1) {
            promoFrom = selected
            promoTo = sq
            promoPopup.open()
        } else {
            selected = ""
        }
    }

    function moveTimeFor(skill) { return 300 + skill * 60 }

    function strengthLabel(s) {
        if (s <= 3) return "Beginner"
        if (s <= 8) return "Casual"
        if (s <= 13) return "Club player"
        if (s <= 18) return "Strong"
        return "Maximum"
    }

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "chess_ui")
                root.ready = isReady && root.backend !== null
        }
    }
    Component.onCompleted: {
        root.ready = root.backend !== null && logos.isViewModuleReady("chess_ui")
    }

    Connections {
        target: root.backend
        enabled: root.backend !== null
        function onHintReady(uciMove) {
            root.hintMove = uciMove
            root.hintPending = false
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // ------------------------------------------------------------------
        // Board
        Item {
            id: boardArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: boardFrame
                readonly property int cell: Math.max(
                    24, Math.floor(Math.min(boardArea.width - 8, boardArea.height - 8) / 8))
                width: cell * 8 + 8
                height: cell * 8 + 8
                anchors.centerIn: parent
                color: "#10131a"
                radius: 6
                border.color: "#2a2f3a"

                Grid {
                    columns: 8
                    anchors.centerIn: parent

                    Repeater {
                        model: 64
                        delegate: Rectangle {
                            readonly property string sq: root.squareAt(index)
                            readonly property bool lightSq: ((Math.floor(index / 8) + index % 8) % 2) === 0
                            readonly property string pc: root.pieces[sq] || ""
                            width: boardFrame.cell
                            height: boardFrame.cell
                            color: lightSq ? "#d8dee9" : "#5b6578"

                            Rectangle {  // last engine/player move
                                anchors.fill: parent
                                color: "#e5c07b"
                                opacity: root.lastMove.length >= 4
                                         && (root.lastMove.substring(0, 2) === sq
                                             || root.lastMove.substring(2, 4) === sq) ? 0.35 : 0
                            }
                            Rectangle {  // hint highlight
                                anchors.fill: parent
                                color: "#61afef"
                                opacity: root.hintMove.length >= 4
                                         && (root.hintMove.substring(0, 2) === sq
                                             || root.hintMove.substring(2, 4) === sq) ? 0.45 : 0
                            }
                            Rectangle {  // king in check
                                anchors.fill: parent
                                color: "#e06c75"
                                opacity: root.checkSquare === sq ? 0.55 : 0
                            }
                            Rectangle {  // selection outline
                                anchors.fill: parent
                                color: "transparent"
                                border.width: 3
                                border.color: "#e5c07b"
                                visible: root.selected === sq
                            }
                            Rectangle {  // legal target marker
                                anchors.centerIn: parent
                                width: pc !== "" ? parent.width * 0.88 : parent.width * 0.3
                                height: width
                                radius: width / 2
                                color: pc !== "" ? "transparent" : "#2ecc71"
                                border.color: "#2ecc71"
                                border.width: pc !== "" ? 3 : 0
                                opacity: 0.6
                                visible: root.targetMap[sq] === true
                            }

                            Text {
                                anchors.centerIn: parent
                                text: root.glyph(pc)
                                font.pixelSize: Math.round(boardFrame.cell * 0.74)
                                color: root.isWhitePiece(pc) ? "#f8f8f2" : "#14161c"
                                style: Text.Outline
                                styleColor: root.isWhitePiece(pc) ? "#14161c" : "#f8f8f2"
                            }

                            Text {  // rank label on left column
                                visible: index % 8 === 0
                                text: sq.charAt(1)
                                font.pixelSize: Math.max(9, Math.round(boardFrame.cell * 0.16))
                                color: lightSq ? "#5b6578" : "#d8dee9"
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 3
                            }
                            Text {  // file label on bottom row
                                visible: Math.floor(index / 8) === 7
                                text: sq.charAt(0)
                                font.pixelSize: Math.max(9, Math.round(boardFrame.cell * 0.16))
                                color: lightSq ? "#5b6578" : "#d8dee9"
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 3
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.clickSquare(sq)
                            }
                        }
                    }
                }

                Rectangle {  // game-over overlay
                    anchors.fill: parent
                    radius: 6
                    color: "#000000"
                    opacity: 0.55
                    visible: root.gameState === "gameOver"
                    MouseArea { anchors.fill: parent }  // swallow board clicks
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 12
                    visible: root.gameState === "gameOver"
                    Text {
                        text: root.statusText
                        color: "#ffffff"
                        font.pixelSize: 22
                        font.bold: true
                        width: boardFrame.width - 48
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Button {
                        text: "New game"
                        enabled: root.ready
                        anchors.horizontalCenter: parent.horizontalCenter
                        onClicked: newGameDialog.open()
                    }
                }
            }
        }

        // ------------------------------------------------------------------
        // Side panel
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.maximumWidth: 250
            Layout.fillHeight: true
            spacing: 10

            Text {
                text: "Logos Chess"
                font.pixelSize: 20
                font.bold: true
                color: "#ffffff"
            }
            Text {
                text: root.engineName !== "" ? root.engineName : "Stockfish"
                font.pixelSize: 12
                color: "#8b949e"
            }
            Text {
                text: root.ready ? "Connected" : "Connecting to backend..."
                color: root.ready ? "#56d364" : "#f0883e"
                font.pixelSize: 11
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: statusRow.implicitHeight + 16
                color: "#151922"
                radius: 6
                border.color: "#2a2f3a"
                RowLayout {
                    id: statusRow
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    BusyIndicator {
                        implicitWidth: 18
                        implicitHeight: 18
                        running: root.gameState === "engineThinking"
                                 || root.gameState === "working"
                                 || root.hintPending
                        visible: running
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.statusText !== "" ? root.statusText : "Waiting for the engine..."
                        wrapMode: Text.WordWrap
                        color: "#e6e9ef"
                        font.pixelSize: 13
                    }
                }
            }

            Text {
                text: root.evalText
                visible: root.evalText !== ""
                color: "#8b949e"
                font.pixelSize: 12
            }

            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 8
                rowSpacing: 8

                Button {
                    Layout.fillWidth: true
                    text: "New game"
                    enabled: root.ready
                    onClicked: newGameDialog.open()
                }
                Button {
                    Layout.fillWidth: true
                    text: "Hint"
                    enabled: root.myTurn
                    onClicked: {
                        root.hintPending = true
                        root.backend.requestHint()
                    }
                }
                Button {
                    Layout.fillWidth: true
                    text: "Undo"
                    enabled: (root.gameState === "playerTurn" || root.gameState === "gameOver")
                             && root.moveHistory !== "" && !root.hintPending
                    onClicked: root.backend.undoMove()
                }
                Button {
                    Layout.fillWidth: true
                    text: "Flip board"
                    onClicked: root.flipped = !root.flipped
                }
            }

            Text {
                text: "Moves"
                font.pixelSize: 12
                color: "#8b949e"
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#151922"
                radius: 6
                border.color: "#2a2f3a"
                Flickable {
                    id: histFlick
                    anchors.fill: parent
                    anchors.margins: 8
                    contentHeight: histText.height
                    clip: true
                    onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
                    Text {
                        id: histText
                        width: histFlick.width
                        text: root.moveHistory !== "" ? root.moveHistory : "No moves yet."
                        color: "#aeb6c2"
                        font.pixelSize: 13
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            // Engine recovery — only shown when Stockfish could not be started.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.gameState === "error"
                TextField {
                    id: enginePathField
                    Layout.fillWidth: true
                    placeholderText: "/path/to/stockfish"
                }
                Button {
                    Layout.fillWidth: true
                    text: "Use engine path"
                    enabled: enginePathField.text.trim() !== ""
                    onClicked: root.backend.setEnginePath(enginePathField.text)
                }
            }
        }
    }

    // ----------------------------------------------------------------------
    // New-game dialog
    Popup {
        id: newGameDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 20

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: "New game"
                font.pixelSize: 17
                font.bold: true
                color: "#ffffff"
            }

            RowLayout {
                spacing: 8
                RadioButton { id: whiteRadio; text: "White"; checked: true }
                RadioButton { id: blackRadio; text: "Black" }
                RadioButton { id: randomRadio; text: "Random" }
            }

            Text {
                text: "Strength: " + Math.round(skillSlider.value)
                      + "  ·  " + root.strengthLabel(Math.round(skillSlider.value))
                font.pixelSize: 13
                color: "#e6e9ef"
            }
            Slider {
                id: skillSlider
                Layout.fillWidth: true
                Layout.minimumWidth: 240
                from: 0
                to: 20
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: 5
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    text: "Cancel"
                    onClicked: newGameDialog.close()
                }
                Button {
                    text: "Start"
                    enabled: root.ready
                    onClicked: {
                        if (!root.backend)
                            return
                        var asWhite = randomRadio.checked ? Math.random() < 0.5
                                                          : whiteRadio.checked
                        var skill = Math.round(skillSlider.value)
                        root.backend.newGame(asWhite, skill, root.moveTimeFor(skill))
                        root.flipped = false
                        root.selected = ""
                        root.hintMove = ""
                        root.hintPending = false
                        newGameDialog.close()
                    }
                }
            }
        }
    }

    // ----------------------------------------------------------------------
    // Promotion picker
    Popup {
        id: promoPopup
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 16

        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Promote to"
                font.pixelSize: 14
                color: "#ffffff"
            }
            RowLayout {
                spacing: 8
                Repeater {
                    model: ["q", "r", "b", "n"]
                    Button {
                        required property string modelData
                        text: root.glyph(modelData)
                        font.pixelSize: 26
                        implicitWidth: 52
                        implicitHeight: 52
                        onClicked: {
                            root.backend.playerMove(root.promoFrom + root.promoTo + modelData)
                            root.selected = ""
                            promoPopup.close()
                        }
                    }
                }
            }
        }
    }
}
