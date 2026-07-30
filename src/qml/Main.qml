import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    // Basecamp sizes the MDI subwindow from the root's sizeHint — without
    // these it falls back to arbitrary dimensions and the view gets clipped.
    implicitWidth: 1080
    implicitHeight: 700

    // Adaptive chrome widths so small canvases keep a playable board.
    readonly property int leftW: width < 1080 ? 168 : 190
    readonly property int panelW: width < 1080 ? 236 : 264

    // Typed replica — auto-synced properties and callable slots.
    readonly property var backend: logos.module("chess_ui")
    property bool ready: false

    // --- backend state (auto-synced PROPs, defensively coerced) ---
    readonly property string mode: ready && backend && backend.mode ? backend.mode : "engine"
    readonly property string fen: ready && backend && backend.fen ? backend.fen : ""
    readonly property string legalMoves: ready && backend && backend.legalMoves ? backend.legalMoves : ""
    readonly property string gameState: ready && backend && backend.gameState ? backend.gameState : "starting"
    readonly property string statusText: ready && backend && backend.status ? backend.status : ""
    readonly property string lastMove: ready && backend && backend.lastMove ? backend.lastMove : ""
    readonly property string sanRows: ready && backend && backend.sanRows ? backend.sanRows : ""
    readonly property string engineLog: ready && backend && backend.engineLog ? backend.engineLog : ""
    readonly property string evalText: ready && backend && backend.evalText ? backend.evalText : ""
    readonly property int evalCp: ready && backend && backend.evalCp !== undefined ? backend.evalCp : 0
    readonly property string engineName: ready && backend && backend.engineName ? backend.engineName : "Stockfish"
    readonly property bool playerIsWhite: ready && backend ? backend.playerIsWhite === true : true
    readonly property bool inCheck: ready && backend ? backend.inCheck === true : false
    readonly property int skill: ready && backend && backend.skill !== undefined ? backend.skill : 4
    readonly property int clockWhiteMs: ready && backend && backend.clockWhiteMs !== undefined ? backend.clockWhiteMs : 0
    readonly property int clockBlackMs: ready && backend && backend.clockBlackMs !== undefined ? backend.clockBlackMs : 0
    readonly property string material: ready && backend && backend.material ? backend.material : "Level"
    readonly property string onlineState: ready && backend && backend.onlineState ? backend.onlineState : "offline"
    readonly property string onlineInfo: ready && backend && backend.onlineInfo ? backend.onlineInfo : ""
    readonly property string gameCode: ready && backend && backend.gameCode ? backend.gameCode : ""
    readonly property string peerName: ready && backend && backend.peerName ? backend.peerName : ""
    readonly property string chatLog: ready && backend && backend.chatLog ? backend.chatLog : ""

    // --- local UI state ---
    property bool flipped: false
    property bool turnBoard: true          // table mode: rotate to whoever is on the clock
    property bool showLegal: true
    property bool markLast: true
    property bool showEvalBar: true
    property int nextTimeMin: 10
    property string whiteName: "White"
    property string blackName: "Black"
    property string selected: ""
    property var targetMap: ({})
    property string hintMove: ""
    property bool hintPending: false
    property string promoFrom: ""
    property string promoTo: ""

    // --- derived ---
    readonly property bool tableMode: mode === "table"
    readonly property bool onlineGame: mode === "online"
    readonly property string sideToMove: fen.split(" ").length > 1 ? fen.split(" ")[1] : "w"
    readonly property bool whiteAtBottom: tableMode
        ? (turnBoard ? sideToMove === "w" : !flipped)
        : (playerIsWhite !== flipped)
    readonly property var pieces: parseFen(fen)
    readonly property var legalList: legalMoves.length ? legalMoves.split(" ") : []
    readonly property bool myTurn: gameState === "playerTurn" && !hintPending
    readonly property bool gameActive: gameState === "playerTurn"
        || gameState === "engineThinking" || gameState === "working"
        || gameState === "opponentTurn"
    readonly property bool untimed: clockWhiteMs <= 0 && clockBlackMs <= 0
        && gameState !== "gameOver"
    readonly property int plies: {
        var t = sanRows.trim()
        if (!t.length)
            return 0
        return t.split(/\s+/).filter(function(tok) { return !tok.endsWith(".") }).length
    }
    readonly property string checkSquare: {
        if (!inCheck)
            return ""
        var king = sideToMove === "w" ? "K" : "k"
        for (var s in pieces)
            if (pieces[s] === king)
                return s
        return ""
    }

    // --- palette ---
    readonly property color cBg: "#0E1013"
    readonly property color cCard: "#15181C"
    readonly property color cCardAlt: "#191C20"
    readonly property color cBorder: "#2A2F35"
    readonly property color cText: "#E8EAED"
    readonly property color cMuted: "#6E747A"
    readonly property color cAccent: "#C9A96A"
    readonly property color cGreen: "#5FA97E"
    readonly property color cRed: "#C96A6A"
    readonly property color cLightSq: "#CBC5B4"
    readonly property color cDarkSq: "#75705F"

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
        var w = { "K": "♔", "Q": "♕", "R": "♖", "B": "♗", "N": "♘", "P": "♙" }
        var b = { "k": "♚", "q": "♛", "r": "♜", "b": "♝", "n": "♞", "p": "♟" }
        if (!p) return ""
        return isWhitePiece(p) ? w[p] : b[p]
    }

    function isWhitePiece(p) { return p !== "" && p === p.toUpperCase() }

    function movablePiece(p) {
        if (!p) return false
        if (tableMode) return isWhitePiece(p) === (sideToMove === "w")
        return isWhitePiece(p) === playerIsWhite
    }

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
            if (movablePiece(pc))
                selected = sq
            return
        }
        if (sq === selected) {
            selected = ""
            return
        }
        if (movablePiece(pc)) {
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

    function fmtClock(ms) {
        if (root.untimed)
            return "∞"
        var s = Math.max(0, Math.round(ms / 1000))
        var m = Math.floor(s / 60)
        var r = s % 60
        return (m < 10 ? "0" : "") + m + ":" + (r < 10 ? "0" : "") + r
    }

    function strengthLabel(s) {
        if (s <= 3) return "beginner"
        if (s <= 8) return "casual"
        if (s <= 13) return "club player"
        if (s <= 18) return "strong"
        return "maximum"
    }

    function startGame(m, asWhite, sk, tmin) {
        if (!root.backend)
            return
        root.nextTimeMin = tmin
        root.backend.newGame(m, asWhite, sk, tmin)
        root.flipped = false
        root.selected = ""
        root.hintMove = ""
        root.hintPending = false
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

    // Dark-styled controls; the default Basic style ships light-gray chrome
    // that fights the theme.
    component DarkButton: Button {
        id: db
        property bool danger: false
        font.pixelSize: 12
        contentItem: Text {
            text: db.text
            font: db.font
            color: !db.enabled ? "#4A5057" : db.danger ? root.cRed : root.cText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 32
            radius: 6
            color: db.down ? "#262B31" : db.hovered ? "#22262B" : "#1D2126"
            border.color: root.cBorder
        }
    }
    component DarkField: TextField {
        color: root.cText
        placeholderTextColor: "#4E5762"
        background: Rectangle {
            implicitHeight: 30
            radius: 6
            color: "#10131A"
            border.color: parent.activeFocus ? root.cAccent : root.cBorder
        }
    }

    Rectangle { anchors.fill: parent; color: root.cBg }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        anchors.bottomMargin: 32
        spacing: 12

        // ==================================================================
        // Left column — kept slim so the board is the centrepiece
        ColumnLayout {
            Layout.preferredWidth: root.leftW
            Layout.maximumWidth: root.leftW
            Layout.fillHeight: true
            spacing: 8

            // Mode switch
            Rectangle {
                Layout.fillWidth: true
                height: 36
                radius: 8
                color: root.cCard
                border.color: root.cBorder
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 4
                    Repeater {
                        model: [{ label: "TABLE", m: "table" },
                                { label: "ENGINE", m: "engine" },
                                { label: "ONLINE", m: "online" }]
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 6
                            color: root.mode === modelData.m ? root.cCardAlt : "transparent"
                            border.color: root.mode === modelData.m ? root.cBorder : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData.label
                                font.pixelSize: 9
                                font.letterSpacing: 1
                                color: root.mode === parent.modelData.m ? root.cText : root.cMuted
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (root.mode !== parent.modelData.m) {
                                        newGameDialog.dialogMode = parent.modelData.m
                                        newGameDialog.open()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Top player card (side shown at top of board)
            Rectangle {
                id: topCard
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                radius: 10
                color: root.cCard
                border.color: root.whiteAtBottom
                    ? (root.sideToMove === "b" && root.gameActive ? root.cAccent : root.cBorder)
                    : (root.sideToMove === "w" && root.gameActive ? root.cAccent : root.cBorder)
                readonly property bool showsWhite: !root.whiteAtBottom
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.tableMode
                                ? (topCard.showsWhite ? root.whiteName : root.blackName)
                                : (topCard.showsWhite === root.playerIsWhite ? "You"
                                   : root.onlineGame
                                     ? (root.peerName || "Opponent") : root.engineName)
                            font.pixelSize: 14
                            font.bold: true
                            color: root.cText
                            elide: Text.ElideRight
                        }
                        Text {
                            text: topCard.showsWhite ? "♙" : "♟"
                            font.pixelSize: 13
                            color: root.cAccent
                        }
                    }
                    Text {
                        text: root.tableMode
                            ? "on the board"
                            : root.onlineGame
                              ? (topCard.showsWhite === root.playerIsWhite
                                 ? "that's you" : "online · code " + root.gameCode)
                              : (topCard.showsWhite === root.playerIsWhite
                                 ? root.nextTimeMin + ":00 game"
                                 : "skill " + root.skill + " · " + root.strengthLabel(root.skill) + " · local")
                        font.pixelSize: 10
                        color: root.cMuted
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: topCard.showsWhite ? "WHITE" : "BLACK"
                            font.pixelSize: 9
                            font.letterSpacing: 2
                            color: root.cMuted
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.fmtClock(topCard.showsWhite ? root.clockWhiteMs : root.clockBlackMs)
                            font.pixelSize: 24
                            font.bold: true
                            color: root.cText
                        }
                    }
                }
            }

            // Bottom player card
            Rectangle {
                id: bottomCard
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                radius: 10
                color: root.cCard
                readonly property bool showsWhite: root.whiteAtBottom
                border.color: (showsWhite ? root.sideToMove === "w" : root.sideToMove === "b")
                              && root.gameActive ? root.cAccent : root.cBorder
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.tableMode
                                ? (bottomCard.showsWhite ? root.whiteName : root.blackName)
                                : (bottomCard.showsWhite === root.playerIsWhite ? "You"
                                   : root.onlineGame
                                     ? (root.peerName || "Opponent") : root.engineName)
                            font.pixelSize: 14
                            font.bold: true
                            color: root.cText
                            elide: Text.ElideRight
                        }
                        Text {
                            text: bottomCard.showsWhite ? "♙" : "♟"
                            font.pixelSize: 13
                            color: root.cAccent
                        }
                    }
                    Text {
                        text: root.tableMode
                            ? "on the board"
                            : (bottomCard.showsWhite === root.playerIsWhite
                               ? (root.untimed ? "" : root.nextTimeMin + ":00 game · ")
                                 + root.plies + " plies"
                               : root.onlineGame ? "online · code " + root.gameCode
                                                 : "skill " + root.skill + " · local")
                        font.pixelSize: 10
                        color: root.cMuted
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: bottomCard.showsWhite ? "WHITE" : "BLACK"
                            font.pixelSize: 9
                            font.letterSpacing: 2
                            color: root.cMuted
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.fmtClock(bottomCard.showsWhite ? root.clockWhiteMs : root.clockBlackMs)
                            font.pixelSize: 24
                            font.bold: true
                            color: root.cText
                        }
                    }
                }
            }

            // Evaluation (engine) / Material (table, online) card
            Rectangle {
                id: statCard
                Layout.fillWidth: true
                Layout.preferredHeight: 84
                radius: 10
                color: root.cCard
                border.color: root.cBorder
                readonly property bool materialCard: root.tableMode || root.onlineGame
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 3
                    Text {
                        text: statCard.materialCard ? "MATERIAL" : "EVALUATION"
                        font.pixelSize: 9
                        font.letterSpacing: 2
                        color: root.cMuted
                    }
                    RowLayout {
                        spacing: 8
                        Text {
                            text: statCard.materialCard
                                ? root.material
                                : (root.evalText.length ? root.evalText.split("  ·  ")[0] : "+0.00")
                            font.pixelSize: 22
                            font.bold: true
                            color: root.cText
                        }
                        Text {
                            text: statCard.materialCard
                                ? (root.material === "Level" ? "even material" : "material edge")
                                : (root.evalCp > 60 ? "White better"
                                   : root.evalCp < -60 ? "Black better" : "balanced")
                            font.pixelSize: 10
                            color: root.cMuted
                        }
                    }
                    Text {
                        text: statCard.materialCard
                            ? (root.onlineGame && root.gameState === "opponentTurn"
                               ? "waiting for " + (root.peerName || "opponent") + "…" : " ")
                            : (root.gameState === "engineThinking" ? "engine thinking…"
                               : root.hintPending ? "calculating hint…" : "engine idle")
                        font.pixelSize: 10
                        color: root.cMuted
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            Item { Layout.fillHeight: true }

            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 8
                rowSpacing: 8
                DarkButton {
                    Layout.fillWidth: true
                    text: "New game"
                    enabled: root.ready
                    onClicked: { newGameDialog.dialogMode = root.mode; newGameDialog.open() }
                }
                DarkButton {
                    Layout.fillWidth: true
                    text: "Hint"
                    enabled: root.myTurn
                    onClicked: { root.hintPending = true; root.backend.requestHint() }
                }
                DarkButton {
                    Layout.fillWidth: true
                    text: "Undo"
                    enabled: (root.gameState === "playerTurn" || root.gameState === "gameOver")
                             && root.sanRows !== "" && !root.hintPending && !root.onlineGame
                    onClicked: root.backend.undoMove()
                }
                DarkButton {
                    Layout.fillWidth: true
                    text: "Flip"
                    enabled: !(root.tableMode && root.turnBoard)
                    onClicked: root.flipped = !root.flipped
                }
            }
        }

        // ==================================================================
        // Eval bar + board, packed beside the left column; the board is sized
        // from the root so content can never exceed the canvas Basecamp gives.

        // Eval bar (engine mode)
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            visible: !root.tableMode && !root.onlineGame && root.showEvalBar
            implicitWidth: 8
            implicitHeight: boardFrame.height - 12
            radius: 4
            color: "#1D1F22"
            border.color: root.cBorder
            Rectangle {
                id: evalFill
                width: parent.width - 2
                x: 1
                radius: 3
                color: "#E8EAED"
                readonly property real frac: Math.max(0.04, Math.min(0.96,
                    0.5 + root.evalCp / 3200))
                height: (parent.height - 2) * frac
                y: root.whiteAtBottom ? parent.height - 1 - height : 1
                Behavior on height { NumberAnimation { duration: 300 } }
            }
        }

        Rectangle {
            id: boardFrame
            Layout.alignment: Qt.AlignVCenter
            readonly property int cell: Math.max(24, Math.floor(
                Math.min(root.height - 84,
                         root.width - root.leftW - root.panelW - 92) / 8))
            implicitWidth: cell * 8 + 8
            implicitHeight: cell * 8 + 8
                    color: "#10131A"
                    radius: 6
                    border.color: root.cBorder

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
                                color: lightSq ? root.cLightSq : root.cDarkSq

                                Rectangle {  // last move
                                    anchors.fill: parent
                                    color: root.cAccent
                                    opacity: root.markLast && root.lastMove.length >= 4
                                             && (root.lastMove.substring(0, 2) === sq
                                                 || root.lastMove.substring(2, 4) === sq) ? 0.35 : 0
                                }
                                Rectangle {  // hint
                                    anchors.fill: parent
                                    color: "#61AFEF"
                                    opacity: root.hintMove.length >= 4
                                             && (root.hintMove.substring(0, 2) === sq
                                                 || root.hintMove.substring(2, 4) === sq) ? 0.45 : 0
                                }
                                Rectangle {  // check
                                    anchors.fill: parent
                                    color: root.cRed
                                    opacity: root.checkSquare === sq ? 0.55 : 0
                                }
                                Rectangle {  // selection
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.width: 3
                                    border.color: root.cAccent
                                    visible: root.selected === sq
                                }
                                Rectangle {  // legal target
                                    anchors.centerIn: parent
                                    width: pc !== "" ? parent.width * 0.88 : parent.width * 0.3
                                    height: width
                                    radius: width / 2
                                    color: pc !== "" ? "transparent" : root.cGreen
                                    border.color: root.cGreen
                                    border.width: pc !== "" ? 3 : 0
                                    opacity: 0.6
                                    visible: root.showLegal && root.targetMap[sq] === true
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: root.glyph(pc)
                                    font.pixelSize: Math.round(boardFrame.cell * 0.76)
                                    color: root.isWhitePiece(pc) ? "#F5F4F0" : "#17181A"
                                    style: Text.Outline
                                    styleColor: root.isWhitePiece(pc) ? "#17181A" : "#00000000"
                                }

                                Text {  // rank label
                                    visible: index % 8 === 0
                                    text: sq.charAt(1)
                                    font.pixelSize: Math.max(10, Math.round(boardFrame.cell * 0.18))
                                    color: lightSq ? root.cDarkSq : root.cLightSq
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.margins: 3
                                }
                                Text {  // file label
                                    visible: Math.floor(index / 8) === 7
                                    text: sq.charAt(0)
                                    font.pixelSize: Math.max(10, Math.round(boardFrame.cell * 0.18))
                                    color: lightSq ? root.cDarkSq : root.cLightSq
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
                        opacity: 0.6
                        visible: root.gameState === "gameOver"
                        MouseArea { anchors.fill: parent }
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 14
                        visible: root.gameState === "gameOver"
                        Text {
                            text: root.statusText
                            color: "#FFFFFF"
                            font.pixelSize: 22
                            font.bold: true
                            width: boardFrame.width - 48
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        DarkButton {
                            text: root.onlineGame ? "leave game" : "play again ↺"
                            anchors.horizontalCenter: parent.horizontalCenter
                            onClicked: {
                                if (root.onlineGame)
                                    root.backend.leaveOnlineGame()
                                else
                                    root.startGame(root.mode, root.playerIsWhite,
                                                   root.skill, root.nextTimeMin)
                            }
                        }
                    }
        }

        Item { Layout.fillWidth: true }

        // ==================================================================
        // Right panel — slim, the board keeps the space
        Rectangle {
            Layout.preferredWidth: root.panelW
            Layout.maximumWidth: root.panelW
            Layout.fillHeight: true
            radius: 10
            color: root.cCard
            border.color: root.cBorder

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                // Tabs
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [root.onlineGame ? "CHAT" : root.tableMode ? "TABLE" : "ENGINE",
                                "MOVES", "SETTINGS"]
                        Rectangle {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            height: 34
                            radius: 6
                            color: rightStack.currentIndex === index ? root.cCardAlt : "transparent"
                            border.color: rightStack.currentIndex === index ? root.cBorder : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData
                                font.pixelSize: 10
                                font.letterSpacing: 2
                                color: rightStack.currentIndex === index ? root.cText : root.cMuted
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: rightStack.currentIndex = parent.index
                            }
                        }
                    }
                }

                StackLayout {
                    id: rightStack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: 0

                    // ------------------------------------------------- tab 0
                    ColumnLayout {
                        spacing: 8

                        // Engine mode: commentary stream. Table mode: clock card.
                        // Online mode: connection card + chat.
                        Flickable {
                            id: logFlick
                            visible: !root.tableMode && !root.onlineGame
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentHeight: logCol.height
                            clip: true
                            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
                            Column {
                                id: logCol
                                width: logFlick.width
                                spacing: 6
                                Repeater {
                                    model: root.engineLog.length ? root.engineLog.split("\n") : []
                                    Rectangle {
                                        required property string modelData
                                        width: Math.min(logCol.width - 20, bubbleText.implicitWidth + 24)
                                        height: bubbleText.implicitHeight + 16
                                        radius: 9
                                        color: root.cCardAlt
                                        border.color: root.cBorder
                                        Text {
                                            id: bubbleText
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            text: parent.modelData
                                            wrapMode: Text.WordWrap
                                            color: root.cText
                                            font.pixelSize: 12
                                        }
                                    }
                                }
                                Text {
                                    text: root.engineName + " · "
                                          + (root.gameState === "engineThinking" ? "thinking…"
                                             : root.ready ? "ready" : "starting")
                                    font.pixelSize: 10
                                    color: root.cMuted
                                }
                            }
                        }

                        ColumnLayout {
                            visible: root.tableMode
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 10

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 118
                                radius: 10
                                color: root.cCardAlt
                                border.color: root.cBorder
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 3
                                    Text {
                                        text: "ON THE CLOCK"
                                        font.pixelSize: 9
                                        font.letterSpacing: 2
                                        color: root.cMuted
                                    }
                                    RowLayout {
                                        spacing: 8
                                        Text {
                                            text: root.sideToMove === "w" ? "♙" : "♟"
                                            font.pixelSize: 20
                                            color: root.cText
                                        }
                                        ColumnLayout {
                                            spacing: 0
                                            Text {
                                                text: root.sideToMove === "w" ? root.whiteName : root.blackName
                                                font.pixelSize: 17
                                                font.bold: true
                                                color: root.cText
                                            }
                                            Text {
                                                text: (root.sideToMove === "w" ? "White" : "Black")
                                                      + " · last move "
                                                      + (root.sanRows.length
                                                         ? root.sanRows.trim().split(/[\s\n]+/).pop() : "—")
                                                font.pixelSize: 10
                                                color: root.cMuted
                                            }
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.turnBoard
                                            ? "The board turns after every move — hand the machine over."
                                            : "The board stays fixed — pass the machine across."
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 11
                                        color: root.cMuted
                                    }
                                }
                            }

                            Text {
                                text: root.statusText
                                font.pixelSize: 12
                                color: root.cText
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                DarkButton {
                                    Layout.fillWidth: true
                                    text: "Agree a draw"
                                    enabled: root.gameActive
                                    onClicked: root.backend.agreeDraw()
                                }
                                DarkButton {
                                    Layout.fillWidth: true
                                    text: "Resign"
                                    enabled: root.gameActive
                                    danger: true
                                    onClicked: root.backend.resign()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: "Turn the board each move"
                                    font.pixelSize: 12
                                    color: root.cText
                                }
                                Switch {
                                    checked: root.turnBoard
                                    onToggled: root.turnBoard = checked
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                DarkField {
                                    Layout.fillWidth: true
                                    placeholderText: "White's name"
                                    text: root.whiteName
                                    font.pixelSize: 11
                                    onEditingFinished: root.whiteName = text.trim() || "White"
                                }
                                DarkField {
                                    Layout.fillWidth: true
                                    placeholderText: "Black's name"
                                    text: root.blackName
                                    font.pixelSize: 11
                                    onEditingFinished: root.blackName = text.trim() || "Black"
                                }
                            }

                            Item { Layout.fillHeight: true }
                        }

                        // Online mode: connection card, chat stream, actions
                        ColumnLayout {
                            visible: root.onlineGame
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: onlineInfoCol.implicitHeight + 20
                                radius: 10
                                color: root.cCardAlt
                                border.color: root.onlineState === "error" ? root.cRed : root.cBorder
                                ColumnLayout {
                                    id: onlineInfoCol
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 2
                                    RowLayout {
                                        Text {
                                            text: root.onlineState === "playing" ? "● connected"
                                                : root.onlineState === "waiting" ? "● waiting"
                                                : root.onlineState === "joining" ? "● searching"
                                                : root.onlineState === "starting" ? "● starting"
                                                : root.onlineState === "error" ? "● error" : "● offline"
                                            font.pixelSize: 10
                                            color: root.onlineState === "playing" ? root.cGreen
                                                 : root.onlineState === "error" ? root.cRed : root.cAccent
                                        }
                                        Item { Layout.fillWidth: true }
                                        Text {
                                            visible: root.gameCode !== ""
                                            text: "code: " + root.gameCode
                                            font.pixelSize: 10
                                            color: root.cMuted
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.onlineInfo !== "" ? root.onlineInfo
                                            : "Host a game or join with a code."
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 11
                                        color: root.cText
                                    }
                                }
                            }

                            Flickable {
                                id: chatFlick
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                contentHeight: chatCol.height
                                clip: true
                                onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
                                Column {
                                    id: chatCol
                                    width: chatFlick.width
                                    spacing: 5
                                    Repeater {
                                        model: root.chatLog.length ? root.chatLog.split("\n") : []
                                        Rectangle {
                                            required property string modelData
                                            width: Math.min(chatCol.width - 12, chatText.implicitWidth + 20)
                                            height: chatText.implicitHeight + 12
                                            radius: 8
                                            color: modelData.indexOf("You:") === 0 ? "#22303A" : root.cCardAlt
                                            border.color: root.cBorder
                                            Text {
                                                id: chatText
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                text: parent.modelData
                                                wrapMode: Text.WordWrap
                                                color: parent.modelData.indexOf("·") === 0
                                                       ? root.cMuted : root.cText
                                                font.pixelSize: 12
                                            }
                                        }
                                    }
                                    Text {
                                        visible: root.chatLog === ""
                                        text: "Chat with your opponent here."
                                        font.pixelSize: 11
                                        color: root.cMuted
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                DarkField {
                                    id: chatInput
                                    Layout.fillWidth: true
                                    enabled: root.onlineState === "playing"
                                    placeholderText: "say something…"
                                    font.pixelSize: 12
                                    onAccepted: {
                                        if (text.trim().length) {
                                            root.backend.sendChat(text)
                                            text = ""
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                DarkButton {
                                    Layout.fillWidth: true
                                    text: "Draw"
                                    enabled: root.gameActive && root.onlineState === "playing"
                                    onClicked: root.backend.agreeDraw()
                                }
                                DarkButton {
                                    Layout.fillWidth: true
                                    text: "Resign"
                                    enabled: root.gameActive && root.onlineState === "playing"
                                    danger: true
                                    onClicked: root.backend.resign()
                                }
                                DarkButton {
                                    Layout.fillWidth: true
                                    text: "Leave"
                                    enabled: root.onlineState !== "offline"
                                    onClicked: root.backend.leaveOnlineGame()
                                }
                            }
                        }

                        // Status line (engine mode)
                        Text {
                            visible: !root.tableMode && !root.onlineGame && root.statusText !== ""
                                     && root.gameState !== "gameOver"
                            Layout.fillWidth: true
                            text: root.statusText
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: root.cMuted
                        }

                        // Typed move input
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "›"; color: root.cAccent; font.pixelSize: 14 }
                            DarkField {
                                id: moveInput
                                Layout.fillWidth: true
                                enabled: root.myTurn
                                placeholderText: root.tableMode
                                    ? "type a move for whoever is on the clock"
                                    : "type a move — Nf3, exd5, O-O"
                                font.pixelSize: 12
                                onAccepted: {
                                    if (text.trim().length) {
                                        root.backend.typedMove(text)
                                        text = ""
                                    }
                                }
                            }
                        }
                    }

                    // ------------------------------------------------- tab 1: moves
                    ColumnLayout {
                        spacing: 6
                        Flickable {
                            id: movesFlick
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentHeight: movesCol.height
                            clip: true
                            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
                            Column {
                                id: movesCol
                                width: movesFlick.width
                                spacing: 2
                                Repeater {
                                    model: root.sanRows.length ? root.sanRows.trim().split("\n") : []
                                    RowLayout {
                                        required property string modelData
                                        width: movesCol.width
                                        spacing: 10
                                        Text {
                                            text: parent.modelData.split(" ")[0] || ""
                                            font.pixelSize: 12
                                            color: root.cMuted
                                            Layout.preferredWidth: 30
                                        }
                                        Text {
                                            text: parent.modelData.split(" ")[1] || ""
                                            font.pixelSize: 13
                                            color: root.cText
                                            Layout.preferredWidth: 80
                                        }
                                        Text {
                                            text: parent.modelData.split(" ")[2] || ""
                                            font.pixelSize: 13
                                            color: root.cText
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                                Text {
                                    visible: root.sanRows.trim() === ""
                                    text: "No moves yet."
                                    font.pixelSize: 12
                                    color: root.cMuted
                                }
                            }
                        }
                    }

                    // ------------------------------------------------- tab 2: settings
                    ColumnLayout {
                        spacing: 12

                        Text {
                            text: "SKILL LEVEL"
                            font.pixelSize: 9
                            font.letterSpacing: 2
                            color: root.cMuted
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Slider {
                                id: skillSlider
                                Layout.fillWidth: true
                                from: 0
                                to: 20
                                stepSize: 1
                                snapMode: Slider.SnapAlways
                                value: root.skill
                                onPressedChanged: {
                                    if (!pressed && Math.round(value) !== root.skill)
                                        root.backend.changeSkill(Math.round(value))
                                }
                                onMoved: {
                                    if (!pressed && Math.round(value) !== root.skill)
                                        root.backend.changeSkill(Math.round(value))
                                }
                            }
                            Text {
                                text: Math.round(skillSlider.value)
                                      + " · " + root.strengthLabel(Math.round(skillSlider.value))
                                font.pixelSize: 11
                                color: root.cText
                                Layout.preferredWidth: 92
                            }
                        }
                        Text {
                            text: "Applies immediately, even mid-game."
                            font.pixelSize: 10
                            color: root.cMuted
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: root.cBorder }

                        Text {
                            text: "BOARD"
                            font.pixelSize: 9
                            font.letterSpacing: 2
                            color: root.cMuted
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: "Show legal moves"; font.pixelSize: 12; color: root.cText }
                            Switch { checked: root.showLegal; onToggled: root.showLegal = checked }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: "Mark the last move"; font.pixelSize: 12; color: root.cText }
                            Switch { checked: root.markLast; onToggled: root.markLast = checked }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: !root.tableMode
                            Text { Layout.fillWidth: true; text: "Evaluation bar"; font.pixelSize: 12; color: root.cText }
                            Switch { checked: root.showEvalBar; onToggled: root.showEvalBar = checked }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: root.cBorder }

                        Text {
                            text: "ENGINE"
                            font.pixelSize: 9
                            font.letterSpacing: 2
                            color: root.cMuted
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            DarkField {
                                id: enginePathField
                                Layout.fillWidth: true
                                placeholderText: "/path/to/stockfish"
                                font.pixelSize: 11
                            }
                            DarkButton {
                                text: "Use"
                                enabled: enginePathField.text.trim() !== ""
                                onClicked: root.backend.setEnginePath(enginePathField.text)
                            }
                        }
                        Text {
                            visible: root.gameState === "error"
                            Layout.fillWidth: true
                            text: root.statusText
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: root.cRed
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    // Footer
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        Text {
            text: "● "
            color: root.ready ? root.cGreen : root.cAccent
            font.pixelSize: 10
        }
        Text {
            text: root.ready ? "Connected" : "Connecting to backend..."
            font.pixelSize: 10
            color: root.cMuted
        }
        Text { text: "·"; font.pixelSize: 10; color: root.cMuted }
        Text { text: "Chess"; font.pixelSize: 10; color: root.cMuted }
        Text { text: "·"; font.pixelSize: 10; color: root.cMuted }
        Text {
            text: (root.onlineGame
                   ? "Online" + (root.peerName ? " vs " + root.peerName : "")
                   : root.tableMode ? "Pass & play" : "vs " + root.engineName)
                  + (root.untimed ? "" : " · " + root.nextTimeMin + ":00 each")
            font.pixelSize: 10
            color: root.cMuted
        }
        Item { Layout.fillWidth: true }
        Text { text: "0.3.0"; font.pixelSize: 10; color: root.cMuted }
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
        background: Rectangle {
            radius: 12
            color: root.cCard
            border.color: root.cBorder
        }
        property string dialogMode: "engine"
        onAboutToShow: {
            dlgSkill.value = root.skill
            timeCombo.currentIndex = [3, 5, 10, 30, 0].indexOf(root.nextTimeMin) === -1
                ? 2 : [3, 5, 10, 30, 0].indexOf(root.nextTimeMin)
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: newGameDialog.dialogMode === "table" ? "New game — two players"
                    : newGameDialog.dialogMode === "online" ? "Play online"
                    : "New game — vs Stockfish"
                font.pixelSize: 16
                font.bold: true
                color: root.cText
            }

            ColumnLayout {
                visible: newGameDialog.dialogMode === "online"
                spacing: 8
                DarkField {
                    id: onlineNameField
                    Layout.fillWidth: true
                    placeholderText: "Your name"
                    font.pixelSize: 12
                }
                RowLayout {
                    spacing: 6
                    DarkField {
                        id: onlineCodeField
                        Layout.fillWidth: true
                        placeholderText: "game code (share it with your opponent)"
                        font.pixelSize: 12
                    }
                    DarkButton {
                        text: "random"
                        onClicked: onlineCodeField.text =
                            Math.random().toString(36).slice(2, 8)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "Both players enter the same code — one hosts, the other joins. "
                          + "Moves and chat travel over Logos Delivery, peer to peer."
                    wrapMode: Text.WordWrap
                    font.pixelSize: 10
                    color: root.cMuted
                }
            }

            RowLayout {
                visible: newGameDialog.dialogMode !== "table"
                spacing: 8
                RadioButton { id: whiteRadio; text: "White"; checked: true }
                RadioButton { id: blackRadio; text: "Black" }
                RadioButton {
                    id: randomRadio
                    visible: newGameDialog.dialogMode === "engine"
                    text: "Random"
                }
            }

            ColumnLayout {
                visible: newGameDialog.dialogMode === "engine"
                spacing: 4
                Text {
                    text: "Strength: " + Math.round(dlgSkill.value)
                          + " · " + root.strengthLabel(Math.round(dlgSkill.value))
                    font.pixelSize: 12
                    color: root.cText
                }
                Slider {
                    id: dlgSkill
                    Layout.fillWidth: true
                    Layout.minimumWidth: 260
                    from: 0
                    to: 20
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: 4
                }
            }

            RowLayout {
                spacing: 8
                Text { text: "Time control"; font.pixelSize: 12; color: root.cText }
                ComboBox {
                    id: timeCombo
                    Layout.preferredWidth: 140
                    model: ["3 min", "5 min", "10 min", "30 min", "Untimed"]
                    currentIndex: 2
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                DarkButton { text: "Cancel"; onClicked: newGameDialog.close() }
                DarkButton {
                    visible: newGameDialog.dialogMode === "online"
                    text: "Join game"
                    enabled: root.ready && onlineCodeField.text.trim() !== ""
                    onClicked: {
                        root.backend.joinOnlineGame(onlineCodeField.text,
                                                    onlineNameField.text)
                        newGameDialog.close()
                    }
                }
                DarkButton {
                    visible: newGameDialog.dialogMode === "online"
                    text: "Host game"
                    enabled: root.ready && onlineCodeField.text.trim() !== ""
                    onClicked: {
                        var mins = [3, 5, 10, 30, 0][timeCombo.currentIndex]
                        root.nextTimeMin = mins
                        root.backend.hostOnlineGame(onlineCodeField.text,
                                                    whiteRadio.checked,
                                                    mins, onlineNameField.text)
                        newGameDialog.close()
                    }
                }
                DarkButton {
                    visible: newGameDialog.dialogMode !== "online"
                    text: "Start"
                    enabled: root.ready
                    onClicked: {
                        var mins = [3, 5, 10, 30, 0][timeCombo.currentIndex]
                        var asWhite = newGameDialog.dialogMode === "table" ? true
                            : (randomRadio.checked ? Math.random() < 0.5 : whiteRadio.checked)
                        root.startGame(newGameDialog.dialogMode, asWhite,
                                       Math.round(dlgSkill.value), mins)
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
        background: Rectangle {
            radius: 12
            color: root.cCard
            border.color: root.cBorder
        }

        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Promote to"
                font.pixelSize: 14
                color: root.cText
            }
            RowLayout {
                spacing: 8
                Repeater {
                    model: ["q", "r", "b", "n"]
                    DarkButton {
                        required property string modelData
                        text: { var g = { "q": "♛", "r": "♜", "b": "♝", "n": "♞" }; return g[modelData] }
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
