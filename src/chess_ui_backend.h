#pragma once

#include "rep_chess_ui_source.h"
#include "logos_ui_plugin_context.h"

#include <QProcess>
#include <QQueue>
#include <QStringList>
#include <QTimer>

// The hand-written UI backend (universal authoring model). The *Plugin and
// *Interface classes — Q_PLUGIN_METADATA, initLogos wiring, QtRO registration
// — are generated around it.
//
// The backend owns a Stockfish process and speaks UCI to it. Stockfish is the
// single source of truth for the rules: legal moves come from `go perft 1`,
// board state and check detection from `d`. Replies from the engine are
// attributed to a FIFO queue of pending requests — Stockfish answers commands
// strictly in the order they were written to stdin, so the head of the queue
// always owns the incoming line.
//
// Two modes: "engine" (vs Stockfish) and "table" (two humans, pass and play).
// In table mode the engine is only the referee: it validates moves and tracks
// state but never searches except for hints.
class ChessUiBackend : public ChessUiSimpleSource,
                       public LogosUiPluginContext
{
public:
    ChessUiBackend();
    ~ChessUiBackend() override;

    // .rep SLOTs
    void newGame(QString gameMode, bool asWhite, int skillLevel, int timeControlMin) override;
    void playerMove(QString uciMove) override;
    void typedMove(QString text) override;
    void undoMove() override;
    void requestHint() override;
    void resign() override;
    void agreeDraw() override;
    void changeSkill(int level) override;
    void setEnginePath(QString path) override;
    void hostOnlineGame(QString code, bool asWhite, int timeControlMin, QString name) override;
    void joinOnlineGame(QString code, QString name) override;
    void leaveOnlineGame() override;
    void sendChat(QString text) override;
    void joinLobby(QString name) override;
    void sendLobbyChat(QString text) override;

    void onContextReady() override;

private:
    enum class Req { Uci, Ready, Display, Perft, BestMove, Hint };
    struct Pending {
        Req type;
        int gen;        // generation at send time; bumped by newGame
        int moveCount;  // position identity at send time (staleness check)
    };

    // engine lifecycle
    void startEngine();
    void stopEngine();
    void engineFailed(const QString& reason);
    QString findEngine() const;
    void send(const QString& line);
    void handleLine(const QString& line);
    void beginSetup();
    void refreshPosition();
    void finishPerft();
    void startThinking();
    void parseEval(const QString& line);
    void applyEngineMove(const QString& mv);
    bool searchPending() const;
    bool handshakePending() const;

    // game state
    void startMatch(const QString& gameMode, bool asWhite, int skillLevel, qint64 tcMs);
    void applyMove(const QString& uciMove, bool broadcast = false);
    void endGame(const QString& text);
    void rebuildSanRows();
    void updateMaterial();
    void appendLog(const QString& line);
    void applySkillWhenIdle();
    QString sideToMove() const;
    bool isPlayersTurn() const;
    bool tableMode() const { return m_mode == QLatin1String("table"); }
    QString colorName(bool white) const;

    // clock
    void tickClock();
    bool clockActive() const;

    // online play over delivery_module
    void ensureDelivery(std::function<void(bool ok, QString detail)> done);
    void teardownOnline(bool notifyPeer);
    void subscribeTopic();
    void publishJson(const QVariantMap& obj);
    void publishTo(const QString& topic, const QVariantMap& obj);
    void handleDeliveryMessage(const QByteArray& payload);
    void handleLobbyMessage(const QByteArray& payload);
    void appendLobby(const QString& line);
    void startOnlineMatch(bool asWhite, int tcMs);
    void sendBeacon();
    void enterOnlineError(const QString& detail);
    void appendChat(const QString& line);
    void setOnline(const QString& state, const QString& info);
    bool onlineMode() const { return m_mode == QLatin1String("online"); }

    QProcess m_proc;
    QTimer m_clockTimer;
    QQueue<Pending> m_queue;
    QStringList m_moves;           // game history, UCI notation
    QStringList m_sans;            // SAN parallel to m_moves (suffix patched late)
    QStringList m_collectedMoves;  // legal moves accumulated from perft output
    QStringList m_legalSanList;    // SAN parallel to m_collectedMoves
    QStringList m_fenKeys;         // repetition keys, index = plies played
    QStringList m_logLines;
    QString m_mode = QStringLiteral("engine");
    QString m_pendingFen;
    QString m_pendingCheckers;
    QString m_enginePathUsed;
    bool m_playerWhite = true;
    int m_skill = 4;
    int m_moveTimeMs = 540;
    int m_gen = 0;
    int m_startId = 0;  // identity of the current engine launch, for the handshake timeout
    qint64 m_whiteMs = 600000;
    qint64 m_blackMs = 600000;
    bool m_untimed = false;
    bool m_engineRunning = false;
    bool m_deferredSetup = false;  // newGame arrived mid-search; run setup after bestmove
    bool m_skillDirty = false;     // setSkill arrived mid-search; apply after bestmove
    bool m_quitting = false;

    // online play
    QTimer m_beaconTimer;
    QStringList m_chatLines;
    QString m_selfId;
    QString m_selfName;
    QString m_topic;
    QString m_pendingRemoteMove;
    int m_onlineGen = 0;           // invalidates async delivery callbacks on leave
    int m_hostTcMin = 10;
    bool m_isHost = false;
    bool m_deliveryReady = false;
    bool m_eventsArmed = false;
    bool m_drawOffered = false;
    bool m_peerOfferedDraw = false;

    // lobby chat (ephemeral, fixed topic)
    QStringList m_lobbyLines;
    bool m_lobbyJoined = false;
};
