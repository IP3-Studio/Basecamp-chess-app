#include "chess_ui_backend.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {
const QString kSettingsOrg = QStringLiteral("Logos");
const QString kSettingsApp = QStringLiteral("chess_ui");
const QString kEnginePathKey = QStringLiteral("enginePath");

// Position identity for repetition counting: piece placement, side to move,
// castling rights, en-passant square — the first four FEN fields.
QString repetitionKey(const QString& fen)
{
    const QStringList fields = fen.split(QLatin1Char(' '));
    return QStringList(fields.mid(0, 4)).join(QLatin1Char(' '));
}

// K vs K, K+B vs K, K+N vs K. Anything more stays in play.
bool insufficientMaterial(const QString& placement)
{
    int minors = 0;
    for (const QChar& c : placement) {
        const QChar l = c.toLower();
        if (l == QLatin1Char('p') || l == QLatin1Char('r') || l == QLatin1Char('q'))
            return false;
        if (l == QLatin1Char('b') || l == QLatin1Char('n'))
            ++minors;
    }
    return minors <= 1;
}
}

ChessUiBackend::ChessUiBackend()
{
    QObject::connect(&m_proc, &QProcess::readyReadStandardOutput, &m_proc, [this]() {
        while (m_proc.canReadLine()) {
            const QString line = QString::fromUtf8(m_proc.readLine()).trimmed();
            if (!line.isEmpty())
                handleLine(line);
        }
    });

    QObject::connect(&m_proc, &QProcess::errorOccurred, &m_proc,
                     [this](QProcess::ProcessError) {
        engineFailed(QStringLiteral("Could not start Stockfish at \"%1\".")
                         .arg(m_enginePathUsed));
    });

    QObject::connect(&m_proc, &QProcess::finished, &m_proc,
                     [this](int, QProcess::ExitStatus) {
        // Any exit outside stopEngine() is a failure — a clean exit(0) from a
        // wrong binary would otherwise leave the game silently stuck.
        if (!m_quitting)
            engineFailed(QStringLiteral("Stockfish stopped unexpectedly."));
    });
}

ChessUiBackend::~ChessUiBackend()
{
    stopEngine();
}

void ChessUiBackend::onContextReady()
{
    startEngine();
}

// ---------------------------------------------------------------------------
// Engine lifecycle

QString ChessUiBackend::findEngine() const
{
    QStringList candidates;

    // The path the user typed into the recovery UI must win — if the env var
    // outranked it, a broken LOGOS_CHESS_STOCKFISH could never be escaped.
    const QString saved =
        QSettings(kSettingsOrg, kSettingsApp).value(kEnginePathKey).toString();
    if (!saved.isEmpty())
        candidates << saved;

    const QString env = qEnvironmentVariable("LOGOS_CHESS_STOCKFISH");
    if (!env.isEmpty())
        candidates << env;

    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("stockfish"));
    if (!onPath.isEmpty())
        candidates << onPath;

#ifdef LOGOS_CHESS_STOCKFISH_BUILD_PATH
    // Plain char array, not QStringLiteral: the path must land in the binary
    // as ASCII so Nix's reference scanner keeps the engine in the closure.
    static const char kBuildEnginePath[] = LOGOS_CHESS_STOCKFISH_BUILD_PATH;
    candidates << QString::fromUtf8(kBuildEnginePath);
#endif

    candidates << QStringLiteral("/opt/homebrew/bin/stockfish")
               << QStringLiteral("/usr/local/bin/stockfish")
               << QStringLiteral("/usr/bin/stockfish")
               << QStringLiteral("/usr/games/stockfish")
               << QDir::homePath() + QStringLiteral("/.nix-profile/bin/stockfish")
               << QStringLiteral("/run/current-system/sw/bin/stockfish");

    for (const QString& c : candidates) {
        const QFileInfo fi(c);
        if (fi.exists() && fi.isFile() && fi.isExecutable())
            return fi.absoluteFilePath();
    }
    return {};
}

void ChessUiBackend::startEngine()
{
    stopEngine();
    if (m_proc.state() != QProcess::NotRunning) {
        engineFailed(QStringLiteral(
            "The previous engine process has not exited yet. Try again in a moment."));
        return;
    }

    const QString path = findEngine();
    if (path.isEmpty()) {
        engineFailed(QStringLiteral("Stockfish was not found on this system."));
        return;
    }

    m_enginePathUsed = path;
    setGameState(QStringLiteral("starting"));
    setStatus(QStringLiteral("Starting Stockfish..."));

    m_proc.start(path, QStringList());
    m_engineRunning = true;
    m_queue.enqueue({Req::Uci, m_gen, 0});
    send(QStringLiteral("uci"));

    // A runnable non-engine (wrong path, wrapper script) never answers
    // "uciok"; without this it would sit in "starting" forever with the
    // recovery UI hidden. Keyed on the launch id, not the game generation —
    // newGame() during a hung handshake bumps the generation but must not
    // disarm this timeout.
    const int startId = ++m_startId;
    QTimer::singleShot(6000, &m_proc, [this, startId]() {
        if (m_engineRunning && startId == m_startId && handshakePending())
            engineFailed(QStringLiteral("\"%1\" did not respond to the UCI handshake.")
                             .arg(m_enginePathUsed));
    });
}

void ChessUiBackend::stopEngine()
{
    m_quitting = true;
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.write("quit\n");
        m_proc.waitForFinished(500);
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.kill();
            m_proc.waitForFinished(2000);
        }
    }
    m_quitting = false;
    m_engineRunning = false;
    m_deferredSetup = false;
    m_queue.clear();
    setEngineReady(false);
}

void ChessUiBackend::engineFailed(const QString& reason)
{
    if (m_quitting)
        return;
    if (m_proc.state() != QProcess::NotRunning) {
        m_quitting = true;
        m_proc.kill();
        m_proc.waitForFinished(1000);
        m_quitting = false;
    }
    m_engineRunning = false;
    m_deferredSetup = false;
    m_queue.clear();
    setEngineReady(false);
    setGameState(QStringLiteral("error"));
    setStatus(reason + QStringLiteral(
        " Install Stockfish (macOS: \"brew install stockfish\", Debian/Ubuntu: "
        "\"apt install stockfish\") or enter the full path to a Stockfish "
        "binary below, then press \"Use engine path\"."));
}

void ChessUiBackend::send(const QString& line)
{
    if (m_proc.state() == QProcess::NotRunning)
        return;
    m_proc.write(line.toUtf8() + '\n');
}

// ---------------------------------------------------------------------------
// .rep SLOTs

void ChessUiBackend::newGame(bool asWhite, int skillLevel, int moveTimeMs)
{
    m_playerWhite = asWhite;
    m_skill = qBound(0, skillLevel, 20);
    m_moveTimeMs = qBound(100, moveTimeMs, 10000);
    ++m_gen;
    m_moves.clear();
    m_collectedMoves.clear();
    m_fenKeys.clear();
    setPlayerIsWhite(m_playerWhite);
    setFen(QString());
    setLastMove(QString());
    setEvalText(QString());
    setLegalMoves(QString());
    setInCheck(false);
    rebuildHistory();

    if (!m_engineRunning || m_proc.state() == QProcess::NotRunning) {
        startEngine();
        return;
    }

    setGameState(QStringLiteral("starting"));
    setStatus(QStringLiteral("Starting a new game..."));
    if (handshakePending()) {
        // The uciok handler will run beginSetup() with the settings above;
        // doing it here too would enqueue two same-generation Ready entries
        // and double every downstream request.
        return;
    }
    if (searchPending()) {
        // Stockfish only handles "stop" while searching; defer the setup
        // commands until the search's bestmove has drained.
        send(QStringLiteral("stop"));
        m_deferredSetup = true;
    } else {
        beginSetup();
    }
}

void ChessUiBackend::playerMove(QString uciMove)
{
    if (gameState() != QLatin1String("playerTurn") || searchPending())
        return;
    if (!m_collectedMoves.contains(uciMove))
        return;
    m_moves << uciMove;
    // Transient state: blocks further input until finishPerft() has validated
    // the new position and published a fresh legal-move list.
    setGameState(QStringLiteral("working"));
    setLastMove(uciMove);
    rebuildHistory();
    refreshPosition();
}

void ChessUiBackend::undoMove()
{
    const QString st = gameState();
    if (st != QLatin1String("playerTurn") && st != QLatin1String("gameOver"))
        return;
    if (searchPending() || m_moves.isEmpty())
        return;
    // As Black the first list entry is the engine's move — with fewer than
    // two moves there is nothing of the player's to take back, and popping
    // would merely re-roll the engine's opening.
    if (m_moves.size() < (m_playerWhite ? 1 : 2))
        return;
    m_moves.removeLast();
    if (!m_moves.isEmpty() && !isPlayersTurn())
        m_moves.removeLast();
    setGameState(QStringLiteral("working"));
    setLastMove(m_moves.isEmpty() ? QString() : m_moves.last());
    rebuildHistory();
    refreshPosition();
}

void ChessUiBackend::requestHint()
{
    if (gameState() != QLatin1String("playerTurn") || searchPending())
        return;
    setStatus(QStringLiteral("Calculating a hint..."));
    // Hints must be full strength even when the opponent is handicapped;
    // the game's skill level is restored when the hint's bestmove arrives.
    send(QStringLiteral("setoption name Skill Level value 20"));
    send(QStringLiteral("go movetime 600"));
    m_queue.enqueue({Req::Hint, m_gen, static_cast<int>(m_moves.size())});
}

void ChessUiBackend::setEnginePath(QString path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return;
    QSettings(kSettingsOrg, kSettingsApp).setValue(kEnginePathKey, trimmed);
    ++m_gen;
    startEngine();
}

// ---------------------------------------------------------------------------
// UCI conversation

void ChessUiBackend::beginSetup()
{
    send(QStringLiteral("setoption name Skill Level value %1").arg(m_skill));
    send(QStringLiteral("ucinewgame"));
    send(QStringLiteral("isready"));
    m_queue.enqueue({Req::Ready, m_gen, static_cast<int>(m_moves.size())});
}

void ChessUiBackend::refreshPosition()
{
    if (!m_engineRunning)
        return;

    QString cmd = QStringLiteral("position startpos");
    if (!m_moves.isEmpty())
        cmd += QStringLiteral(" moves ") + m_moves.join(QLatin1Char(' '));
    send(cmd);

    m_pendingFen.clear();
    m_pendingCheckers.clear();
    send(QStringLiteral("d"));
    m_queue.enqueue({Req::Display, m_gen, static_cast<int>(m_moves.size())});

    m_collectedMoves.clear();
    send(QStringLiteral("go perft 1"));
    m_queue.enqueue({Req::Perft, m_gen, static_cast<int>(m_moves.size())});
}

void ChessUiBackend::handleLine(const QString& line)
{
    if (m_queue.isEmpty())
        return;  // startup banner and other unsolicited chatter

    const Pending p = m_queue.head();
    const bool fresh = p.gen == m_gen && p.moveCount == m_moves.size();

    switch (p.type) {
    case Req::Uci:
        if (line.startsWith(QLatin1String("id name ")))
            setEngineName(line.mid(8));
        else if (line == QLatin1String("uciok")) {
            m_queue.dequeue();
            beginSetup();
        }
        break;

    case Req::Ready:
        if (line == QLatin1String("readyok")) {
            m_queue.dequeue();
            if (fresh) {
                setEngineReady(true);
                refreshPosition();
            }
        }
        break;

    case Req::Display:
        if (line.startsWith(QLatin1String("Fen: "))) {
            m_pendingFen = line.mid(5).trimmed();
        } else if (line.startsWith(QLatin1String("Checkers:"))) {
            m_pendingCheckers = line.mid(9).trimmed();
            m_queue.dequeue();
            if (fresh && !m_pendingFen.isEmpty()) {
                setFen(m_pendingFen);
                setInCheck(!m_pendingCheckers.isEmpty());
                // Keyed by ply so an undo simply truncates the history.
                if (m_fenKeys.size() > p.moveCount)
                    m_fenKeys.resize(p.moveCount);
                m_fenKeys.append(repetitionKey(m_pendingFen));
            }
        }
        break;

    case Req::Perft: {
        static const QRegularExpression moveLine(
            QStringLiteral("^([a-h][1-8][a-h][1-8][qrbn]?): \\d+$"));
        const auto m = moveLine.match(line);
        if (m.hasMatch()) {
            m_collectedMoves << m.captured(1);
        } else if (line.startsWith(QLatin1String("Nodes searched:"))) {
            m_queue.dequeue();
            if (fresh)
                finishPerft();
        }
        break;
    }

    case Req::BestMove:
    case Req::Hint:
        if (line.startsWith(QLatin1String("info "))) {
            if (fresh && p.type == Req::BestMove)
                parseEval(line);
        } else if (line.startsWith(QLatin1String("bestmove"))) {
            m_queue.dequeue();
            if (p.type == Req::Hint)
                send(QStringLiteral("setoption name Skill Level value %1").arg(m_skill));
            if (m_deferredSetup && !searchPending()) {
                m_deferredSetup = false;
                beginSetup();
                break;
            }
            if (!fresh)
                break;
            const QString best = line.section(QLatin1Char(' '), 1, 1);
            if (p.type == Req::BestMove) {
                if (best != QLatin1String("(none)"))
                    applyEngineMove(best);
            } else {
                emit hintReady(best);
                setStatus(QStringLiteral("Hint: %1 — your move.").arg(best));
            }
        }
        break;
    }
}

void ChessUiBackend::finishPerft()
{
    setLegalMoves(m_collectedMoves.join(QLatin1Char(' ')));

    if (m_collectedMoves.isEmpty()) {
        setGameState(QStringLiteral("gameOver"));
        if (!m_pendingCheckers.isEmpty())
            setStatus(isPlayersTurn()
                          ? QStringLiteral("Checkmate — Stockfish wins.")
                          : QStringLiteral("Checkmate — you win!"));
        else
            setStatus(QStringLiteral("Stalemate — it's a draw."));
        return;
    }

    const QStringList fenFields = fen().split(QLatin1Char(' '));
    if (fenFields.size() >= 5 && fenFields.at(4).toInt() >= 100) {
        setGameState(QStringLiteral("gameOver"));
        setStatus(QStringLiteral("Draw by the fifty-move rule."));
        return;
    }

    if (!fenFields.isEmpty() && insufficientMaterial(fenFields.first())) {
        setGameState(QStringLiteral("gameOver"));
        setStatus(QStringLiteral("Draw — insufficient material."));
        return;
    }

    if (m_fenKeys.count(repetitionKey(fen())) >= 3) {
        setGameState(QStringLiteral("gameOver"));
        setStatus(QStringLiteral("Draw by threefold repetition."));
        return;
    }

    if (isPlayersTurn()) {
        setGameState(QStringLiteral("playerTurn"));
        setStatus(inCheck() ? QStringLiteral("Your move — check!")
                            : QStringLiteral("Your move."));
    } else {
        startThinking();
    }
}

void ChessUiBackend::startThinking()
{
    setGameState(QStringLiteral("engineThinking"));
    setStatus(QStringLiteral("Stockfish is thinking..."));
    send(QStringLiteral("go movetime %1").arg(m_moveTimeMs));
    m_queue.enqueue({Req::BestMove, m_gen, static_cast<int>(m_moves.size())});
}

void ChessUiBackend::parseEval(const QString& line)
{
    static const QRegularExpression reDepth(QStringLiteral(" depth (\\d+)"));
    static const QRegularExpression reCp(QStringLiteral(" score cp (-?\\d+)"));
    static const QRegularExpression reMate(QStringLiteral(" score mate (-?\\d+)"));

    // UCI scores are from the side to move; normalise to White's perspective.
    const bool whiteToMove = sideToMove() == QLatin1String("w");
    const auto dm = reDepth.match(line);
    const QString depth = dm.hasMatch() ? dm.captured(1) : QString();

    const auto cm = reCp.match(line);
    if (cm.hasMatch()) {
        double v = cm.captured(1).toInt() / 100.0;
        if (!whiteToMove)
            v = -v;
        QString text = QStringLiteral("Eval %1%2").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 2);
        if (!depth.isEmpty())
            text += QStringLiteral("  ·  depth %1").arg(depth);
        setEvalText(text);
        return;
    }

    const auto mm = reMate.match(line);
    if (mm.hasMatch()) {
        const int n = mm.captured(1).toInt();
        const bool whiteMates = (n > 0) == whiteToMove;
        setEvalText(QStringLiteral("Mate in %1 for %2")
                        .arg(qAbs(n))
                        .arg(whiteMates ? QStringLiteral("White")
                                        : QStringLiteral("Black")));
    }
}

void ChessUiBackend::applyEngineMove(const QString& mv)
{
    m_moves << mv;
    setLastMove(mv);
    rebuildHistory();
    refreshPosition();
}

// ---------------------------------------------------------------------------
// Helpers

void ChessUiBackend::rebuildHistory()
{
    QString h;
    for (int i = 0; i < m_moves.size(); ++i) {
        if (i % 2 == 0)
            h += QString::number(i / 2 + 1) + QStringLiteral(". ");
        h += m_moves.at(i) + (i % 2 == 0 ? QStringLiteral("  ") : QStringLiteral("\n"));
    }
    setMoveHistory(h);
}

bool ChessUiBackend::searchPending() const
{
    for (const Pending& p : m_queue)
        if (p.type == Req::BestMove || p.type == Req::Hint)
            return true;
    return false;
}

bool ChessUiBackend::handshakePending() const
{
    for (const Pending& p : m_queue)
        if (p.type == Req::Uci)
            return true;
    return false;
}

QString ChessUiBackend::sideToMove() const
{
    return m_moves.size() % 2 == 0 ? QStringLiteral("w") : QStringLiteral("b");
}

bool ChessUiBackend::isPlayersTurn() const
{
    return (sideToMove() == QLatin1String("w")) == m_playerWhite;
}
