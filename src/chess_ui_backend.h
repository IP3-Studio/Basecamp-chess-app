#pragma once

#include "rep_chess_ui_source.h"
#include "logos_ui_plugin_context.h"

#include <QProcess>
#include <QQueue>
#include <QStringList>

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
class ChessUiBackend : public ChessUiSimpleSource,
                       public LogosUiPluginContext
{
public:
    ChessUiBackend();
    ~ChessUiBackend() override;

    // .rep SLOTs
    void newGame(bool asWhite, int skillLevel, int moveTimeMs) override;
    void playerMove(QString uciMove) override;
    void undoMove() override;
    void requestHint() override;
    void setEnginePath(QString path) override;

    void onContextReady() override;

private:
    enum class Req { Uci, Ready, Display, Perft, BestMove, Hint };
    struct Pending {
        Req type;
        int gen;        // generation at send time; bumped by newGame
        int moveCount;  // position identity at send time (staleness check)
    };

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
    void rebuildHistory();
    bool searchPending() const;
    bool handshakePending() const;
    QString sideToMove() const;
    bool isPlayersTurn() const;

    QProcess m_proc;
    QQueue<Pending> m_queue;
    QStringList m_moves;           // game history, UCI notation
    QStringList m_collectedMoves;  // legal moves accumulated from perft output
    QStringList m_fenKeys;         // repetition keys, index = plies played
    QString m_pendingFen;
    QString m_pendingCheckers;
    QString m_enginePathUsed;
    bool m_playerWhite = true;
    int m_skill = 5;
    int m_moveTimeMs = 900;
    int m_gen = 0;
    int m_startId = 0;  // identity of the current engine launch, for the handshake timeout
    bool m_engineRunning = false;
    bool m_deferredSetup = false;  // newGame arrived mid-search; run setup after bestmove
    bool m_quitting = false;
};
