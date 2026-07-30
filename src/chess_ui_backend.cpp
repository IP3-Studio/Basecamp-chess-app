#include "chess_ui_backend.h"

// Generated umbrella: LogosModules (behind modules()) from
// metadata.json#dependencies — typed wrappers + typed event accessors.
#include "logos_sdk.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>

#include <cctype>

namespace {
const QString kSettingsOrg = QStringLiteral("Logos");
const QString kSettingsApp = QStringLiteral("chess_ui");
const QString kEnginePathKey = QStringLiteral("enginePath");
const int kLogCap = 80;

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

// Board indexed a1=0 .. h8=63 (rank * 8 + file).
struct Board {
    char sq[64] = {};
};

Board parseBoard(const QString& fen)
{
    Board b;
    int rank = 7;
    int file = 0;
    const QString placement = fen.section(QLatin1Char(' '), 0, 0);
    for (const QChar& c : placement) {
        if (c == QLatin1Char('/')) {
            --rank;
            file = 0;
        } else if (c.isDigit()) {
            file += c.digitValue();
        } else {
            if (rank >= 0 && rank < 8 && file >= 0 && file < 8)
                b.sq[rank * 8 + file] = c.toLatin1();
            ++file;
        }
    }
    return b;
}

int fileOf(const QString& sq) { return sq.at(0).toLatin1() - 'a'; }
int rankOf(const QString& sq) { return sq.at(1).toLatin1() - '1'; }

// Standard algebraic notation for a UCI move in the given position. Check and
// mate suffixes are patched later, once the resulting position is known.
QString sanForMove(const Board& b, const QString& uci, const QStringList& allLegal)
{
    const QString from = uci.mid(0, 2);
    const QString to = uci.mid(2, 2);
    const int ff = fileOf(from);
    const int fr = rankOf(from);
    const int tf = fileOf(to);
    const int tr = rankOf(to);
    const char piece = b.sq[fr * 8 + ff];
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(piece)));

    if (upper == 'K' && qAbs(tf - ff) == 2)
        return tf > ff ? QStringLiteral("O-O") : QStringLiteral("O-O-O");

    const bool isPawn = upper == 'P';
    const bool capture = b.sq[tr * 8 + tf] != 0 || (isPawn && tf != ff);

    QString san;
    if (isPawn) {
        if (capture) {
            san += QLatin1Char(static_cast<char>('a' + ff));
            san += QLatin1Char('x');
        }
        san += to;
        if (uci.size() == 5) {
            san += QLatin1Char('=');
            san += uci.at(4).toUpper();
        }
        return san;
    }

    san += QLatin1Char(upper);

    // Disambiguation: file if unique, else rank, else both.
    bool any = false, fileClash = false, rankClash = false;
    for (const QString& other : allLegal) {
        if (other == uci || other.mid(2, 2) != to)
            continue;
        const int of = fileOf(other);
        const int orr = rankOf(other);
        const char op = b.sq[orr * 8 + of];
        if (std::toupper(static_cast<unsigned char>(op)) != upper)
            continue;
        any = true;
        if (of == ff) fileClash = true;
        if (orr == fr) rankClash = true;
    }
    if (any) {
        if (!fileClash)
            san += QLatin1Char(static_cast<char>('a' + ff));
        else if (!rankClash)
            san += QLatin1Char(static_cast<char>('1' + fr));
        else
            san += from;
    }
    if (capture)
        san += QLatin1Char('x');
    san += to;
    return san;
}

// Loose normalisation for typed-move matching.
QString normalizedSan(QString s)
{
    s = s.trimmed();
    s.remove(QLatin1Char('+'));
    s.remove(QLatin1Char('#'));
    s.remove(QLatin1Char('x'));
    s.remove(QLatin1Char('='));
    s.replace(QLatin1Char('0'), QLatin1Char('O'));
    return s;
}

int materialDiff(const QString& placement)
{
    int diff = 0;
    for (const QChar& c : placement) {
        int v = 0;
        switch (c.toLower().toLatin1()) {
        case 'p': v = 1; break;
        case 'n': case 'b': v = 3; break;
        case 'r': v = 5; break;
        case 'q': v = 9; break;
        default: continue;
        }
        diff += c.isUpper() ? v : -v;
    }
    return diff;
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

    m_clockTimer.setInterval(200);
    QObject::connect(&m_clockTimer, &QTimer::timeout, &m_clockTimer,
                     [this]() { tickClock(); });
    m_clockTimer.start();

    m_beaconTimer.setInterval(4000);
    QObject::connect(&m_beaconTimer, &QTimer::timeout, &m_beaconTimer,
                     [this]() { sendBeacon(); });

    m_selfId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
        "binary in Settings."));
}

void ChessUiBackend::send(const QString& line)
{
    if (m_proc.state() == QProcess::NotRunning)
        return;
    m_proc.write(line.toUtf8() + '\n');
}

// ---------------------------------------------------------------------------
// .rep SLOTs

void ChessUiBackend::newGame(QString gameMode, bool asWhite, int skillLevel, int timeControlMin)
{
    if (onlineMode() && gameMode != QLatin1String("online"))
        teardownOnline(true);
    const QString m = gameMode == QLatin1String("table") ? QStringLiteral("table")
                                                         : QStringLiteral("engine");
    startMatch(m, asWhite, skillLevel,
               timeControlMin <= 0 ? 0 : qint64(timeControlMin) * 60000);
}

void ChessUiBackend::startMatch(const QString& gameMode, bool asWhite, int skillLevel, qint64 tcMs)
{
    m_mode = gameMode;
    setMode(m_mode);
    m_playerWhite = asWhite;
    m_skill = qBound(0, skillLevel, 20);
    setSkill(m_skill);
    m_moveTimeMs = 300 + m_skill * 60;
    m_untimed = tcMs <= 0;
    m_whiteMs = m_blackMs = m_untimed ? 0 : tcMs;
    setClockWhiteMs(int(m_whiteMs));
    setClockBlackMs(int(m_blackMs));

    ++m_gen;
    m_moves.clear();
    m_sans.clear();
    m_collectedMoves.clear();
    m_legalSanList.clear();
    m_fenKeys.clear();
    m_logLines.clear();
    setEngineLog(QString());
    setPlayerIsWhite(m_playerWhite);
    setFen(QString());
    setLastMove(QString());
    setEvalText(QString());
    setEvalCp(0);
    setLegalMoves(QString());
    setLegalSans(QString());
    setInCheck(false);
    rebuildSanRows();
    updateMaterial();

    if (tableMode())
        appendLog(QStringLiteral("The table is set. White to play."));
    else if (onlineMode())
        appendLog(QStringLiteral("Online game vs %1 — you play %2.")
                      .arg(peerName().isEmpty() ? QStringLiteral("your opponent") : peerName())
                      .arg(colorName(m_playerWhite)));
    else
        appendLog(QStringLiteral("Loaded. Skill %1. %2 to move.")
                      .arg(m_skill)
                      .arg(QStringLiteral("White")));

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
    applyMove(uciMove, true);
}

void ChessUiBackend::typedMove(QString text)
{
    if (gameState() != QLatin1String("playerTurn") || searchPending())
        return;
    const QString raw = text.trimmed();
    if (raw.isEmpty())
        return;

    // UCI form first (e2e4, e7e8q).
    const QString lowered = raw.toLower();
    if (m_collectedMoves.contains(lowered)) {
        applyMove(lowered, true);
        return;
    }

    // SAN, exact case; fall back to case-insensitive only when unambiguous
    // (case matters: bxc3 is a pawn capture, Bxc3 a bishop move).
    const QString wanted = normalizedSan(raw);
    QStringList caseHits, looseHits;
    for (int i = 0; i < m_legalSanList.size(); ++i) {
        const QString cand = normalizedSan(m_legalSanList.at(i));
        if (cand == wanted)
            caseHits << m_collectedMoves.at(i);
        if (cand.compare(wanted, Qt::CaseInsensitive) == 0)
            looseHits << m_collectedMoves.at(i);
    }
    if (caseHits.size() == 1) {
        applyMove(caseHits.first(), true);
        return;
    }
    if (caseHits.isEmpty() && looseHits.size() == 1) {
        applyMove(looseHits.first(), true);
        return;
    }
    setStatus(caseHits.size() > 1 || looseHits.size() > 1
                  ? QStringLiteral("\"%1\" is ambiguous — add the file or rank (e.g. Nbd2).").arg(raw)
                  : QStringLiteral("\"%1\" is not a legal move here.").arg(raw));
}

void ChessUiBackend::undoMove()
{
    if (onlineMode())
        return;  // no takeback protocol between peers
    const QString st = gameState();
    if (st != QLatin1String("playerTurn") && st != QLatin1String("gameOver"))
        return;
    if (searchPending() || m_moves.isEmpty())
        return;
    if (tableMode()) {
        m_moves.removeLast();
        m_sans.removeLast();
    } else {
        // As Black the first list entry is the engine's move — with fewer than
        // two moves there is nothing of the player's to take back, and popping
        // would merely re-roll the engine's opening.
        if (m_moves.size() < (m_playerWhite ? 1 : 2))
            return;
        m_moves.removeLast();
        m_sans.removeLast();
        if (!m_moves.isEmpty() && !isPlayersTurn()) {
            m_moves.removeLast();
            m_sans.removeLast();
        }
    }
    setGameState(QStringLiteral("working"));
    setLastMove(m_moves.isEmpty() ? QString() : m_moves.last());
    rebuildSanRows();
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

void ChessUiBackend::resign()
{
    const QString st = gameState();
    if (st != QLatin1String("playerTurn") && st != QLatin1String("engineThinking")
        && st != QLatin1String("working") && st != QLatin1String("opponentTurn"))
        return;
    if (searchPending())
        send(QStringLiteral("stop"));
    ++m_gen;
    if (onlineMode()) {
        publishJson({{QStringLiteral("t"), QStringLiteral("resign")}});
        endGame(QStringLiteral("You resigned — %1 wins.")
                    .arg(peerName().isEmpty() ? QStringLiteral("your opponent") : peerName()));
    } else if (tableMode()) {
        endGame(QStringLiteral("%1 resigns — %2 wins.")
                    .arg(colorName(sideToMove() == QLatin1String("w")))
                    .arg(colorName(sideToMove() != QLatin1String("w"))));
    } else {
        endGame(QStringLiteral("You resigned — Stockfish wins."));
    }
}

void ChessUiBackend::agreeDraw()
{
    const QString st = gameState();
    if (st != QLatin1String("playerTurn") && st != QLatin1String("working")
        && st != QLatin1String("opponentTurn"))
        return;
    if (onlineMode()) {
        if (m_peerOfferedDraw) {
            publishJson({{QStringLiteral("t"), QStringLiteral("drawAccept")}});
            ++m_gen;
            endGame(QStringLiteral("Draw agreed."));
        } else if (!m_drawOffered) {
            m_drawOffered = true;
            publishJson({{QStringLiteral("t"), QStringLiteral("drawOffer")}});
            appendChat(QStringLiteral("· You offered a draw."));
            setStatus(QStringLiteral("Draw offered — waiting for %1.").arg(peerName()));
        }
        return;
    }
    if (!tableMode())
        return;
    ++m_gen;
    endGame(QStringLiteral("Draw agreed."));
}

void ChessUiBackend::changeSkill(int level)
{
    m_skill = qBound(0, level, 20);
    m_moveTimeMs = 300 + m_skill * 60;
    setSkill(m_skill);
    appendLog(QStringLiteral("Skill set to %1.").arg(m_skill));
    if (!m_engineRunning)
        return;
    if (searchPending() || handshakePending())
        m_skillDirty = true;  // applied once the current search drains
    else
        send(QStringLiteral("setoption name Skill Level value %1").arg(m_skill));
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
    m_skillDirty = false;
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
            else if (m_skillDirty && !searchPending()) {
                m_skillDirty = false;
                send(QStringLiteral("setoption name Skill Level value %1").arg(m_skill));
            }
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
                const QString san = sanForMove(parseBoard(fen()), best, m_collectedMoves);
                emit hintReady(best);
                appendLog(QStringLiteral("Hint: %1.").arg(san));
                setStatus(QStringLiteral("Hint: %1 — your move.").arg(san));
            }
        }
        break;
    }
}

void ChessUiBackend::finishPerft()
{
    setLegalMoves(m_collectedMoves.join(QLatin1Char(' ')));

    const Board board = parseBoard(fen());
    m_legalSanList.clear();
    for (const QString& mv : m_collectedMoves)
        m_legalSanList << sanForMove(board, mv, m_collectedMoves);
    setLegalSans(m_legalSanList.join(QLatin1Char(' ')));

    // Patch the previous move's SAN with + / # now the reply is known.
    if (!m_sans.isEmpty() && m_sans.size() == m_moves.size()) {
        QString& last = m_sans.last();
        if (!last.endsWith(QLatin1Char('+')) && !last.endsWith(QLatin1Char('#'))
            && inCheck()) {
            last += m_collectedMoves.isEmpty() ? QLatin1Char('#') : QLatin1Char('+');
            rebuildSanRows();
        }
    }

    updateMaterial();

    if (m_collectedMoves.isEmpty()) {
        if (!m_pendingCheckers.isEmpty()) {
            const bool whiteMated = sideToMove() == QLatin1String("w");
            const QString opp = onlineMode()
                ? (peerName().isEmpty() ? QStringLiteral("Your opponent") : peerName())
                : QStringLiteral("Stockfish");
            if (tableMode())
                endGame(QStringLiteral("Checkmate — %1 wins.").arg(colorName(!whiteMated)));
            else
                endGame(isPlayersTurn() ? QStringLiteral("Checkmate — %1 wins.").arg(opp)
                                        : QStringLiteral("Checkmate — you win!"));
        } else {
            endGame(QStringLiteral("Stalemate — it's a draw."));
        }
        return;
    }

    const QStringList fenFields = fen().split(QLatin1Char(' '));
    if (fenFields.size() >= 5 && fenFields.at(4).toInt() >= 100) {
        endGame(QStringLiteral("Draw by the fifty-move rule."));
        return;
    }

    if (!fenFields.isEmpty() && insufficientMaterial(fenFields.first())) {
        endGame(QStringLiteral("Draw — insufficient material."));
        return;
    }

    if (m_fenKeys.count(repetitionKey(fen())) >= 3) {
        endGame(QStringLiteral("Draw by threefold repetition."));
        return;
    }

    if (tableMode() || isPlayersTurn()) {
        setGameState(QStringLiteral("playerTurn"));
        const QString mover = tableMode()
            ? colorName(sideToMove() == QLatin1String("w"))
            : QStringLiteral("Your");
        if (tableMode())
            setStatus(inCheck() ? QStringLiteral("%1 to play — check!").arg(mover)
                                : QStringLiteral("%1 to play.").arg(mover));
        else
            setStatus(inCheck() ? QStringLiteral("Your move — check!")
                                : QStringLiteral("Your move."));
    } else if (onlineMode()) {
        setGameState(QStringLiteral("opponentTurn"));
        setStatus(QStringLiteral("Waiting for %1...")
                      .arg(peerName().isEmpty() ? QStringLiteral("your opponent") : peerName()));
        if (!m_pendingRemoteMove.isEmpty()) {
            const QString mv = m_pendingRemoteMove;
            m_pendingRemoteMove.clear();
            if (m_collectedMoves.contains(mv))
                applyMove(mv);
            else
                enterOnlineError(QStringLiteral("The game went out of sync — please start a new one."));
        }
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
        int cp = cm.captured(1).toInt();
        if (!whiteToMove)
            cp = -cp;
        setEvalCp(qBound(-1500, cp, 1500));
        const double v = cp / 100.0;
        QString text = QStringLiteral("%1%2").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 2);
        if (!depth.isEmpty())
            text += QStringLiteral("  ·  depth %1").arg(depth);
        setEvalText(text);
        return;
    }

    const auto mm = reMate.match(line);
    if (mm.hasMatch()) {
        const int n = mm.captured(1).toInt();
        const bool whiteMates = (n > 0) == whiteToMove;
        setEvalCp(whiteMates ? 1500 : -1500);
        setEvalText(QStringLiteral("Mate in %1 for %2")
                        .arg(qAbs(n))
                        .arg(whiteMates ? QStringLiteral("White")
                                        : QStringLiteral("Black")));
    }
}

void ChessUiBackend::applyEngineMove(const QString& mv)
{
    const QString san = sanForMove(parseBoard(fen()), mv, m_collectedMoves);
    appendLog(QStringLiteral("Stockfish plays %1.").arg(san));
    applyMove(mv);
}

// ---------------------------------------------------------------------------
// Game state

void ChessUiBackend::applyMove(const QString& uciMove, bool broadcast)
{
    const QString san = sanForMove(parseBoard(fen()), uciMove, m_collectedMoves);
    m_moves << uciMove;
    m_sans << san;
    // Transient state: blocks further input until finishPerft() has validated
    // the new position and published a fresh legal-move list.
    setGameState(QStringLiteral("working"));
    setLastMove(uciMove);
    rebuildSanRows();

    if (broadcast && onlineMode()) {
        const qint64 myClock = m_playerWhite ? m_whiteMs : m_blackMs;
        publishJson({{QStringLiteral("t"), QStringLiteral("move")},
                     {QStringLiteral("ply"), m_moves.size() - 1},
                     {QStringLiteral("uci"), uciMove},
                     {QStringLiteral("clockMs"), myClock}});
    }
    refreshPosition();
}

void ChessUiBackend::endGame(const QString& text)
{
    setGameState(QStringLiteral("gameOver"));
    setStatus(text);
    appendLog(text);
}

void ChessUiBackend::rebuildSanRows()
{
    QString rows;
    for (int i = 0; i < m_sans.size(); ++i) {
        if (i % 2 == 0)
            rows += QString::number(i / 2 + 1) + QStringLiteral(". ") + m_sans.at(i);
        else
            rows += QLatin1Char(' ') + m_sans.at(i) + QLatin1Char('\n');
    }
    if (m_sans.size() % 2 == 1)
        rows += QLatin1Char('\n');
    setSanRows(rows);
}

void ChessUiBackend::updateMaterial()
{
    const QString placement = fen().section(QLatin1Char(' '), 0, 0);
    if (placement.isEmpty()) {
        setMaterial(QStringLiteral("Level"));
        return;
    }
    const int diff = materialDiff(placement);
    if (diff == 0)
        setMaterial(QStringLiteral("Level"));
    else if (diff > 0)
        setMaterial(QStringLiteral("White +%1").arg(diff));
    else
        setMaterial(QStringLiteral("Black +%1").arg(-diff));
}

void ChessUiBackend::appendLog(const QString& line)
{
    m_logLines << line;
    while (m_logLines.size() > kLogCap)
        m_logLines.removeFirst();
    setEngineLog(m_logLines.join(QLatin1Char('\n')));
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
    if (tableMode())
        return true;
    return (sideToMove() == QLatin1String("w")) == m_playerWhite;
}

QString ChessUiBackend::colorName(bool white) const
{
    return white ? QStringLiteral("White") : QStringLiteral("Black");
}

// ---------------------------------------------------------------------------
// Clock

bool ChessUiBackend::clockActive() const
{
    if (m_untimed)
        return false;
    const QString st = gameState();
    return st == QLatin1String("playerTurn") || st == QLatin1String("engineThinking")
        || st == QLatin1String("working") || st == QLatin1String("opponentTurn");
}

void ChessUiBackend::tickClock()
{
    if (!clockActive())
        return;
    const bool whiteToMove = sideToMove() == QLatin1String("w");
    qint64& c = whiteToMove ? m_whiteMs : m_blackMs;
    c = qMax<qint64>(0, c - m_clockTimer.interval());
    if (whiteToMove)
        setClockWhiteMs(int(c));
    else
        setClockBlackMs(int(c));
    if (c == 0) {
        ++m_gen;
        if (searchPending())
            send(QStringLiteral("stop"));
        const QString opp = onlineMode()
            ? (peerName().isEmpty() ? QStringLiteral("your opponent") : peerName())
            : QStringLiteral("Stockfish");
        if (tableMode())
            endGame(QStringLiteral("%1 ran out of time — %2 wins.")
                        .arg(colorName(whiteToMove))
                        .arg(colorName(!whiteToMove)));
        else if (whiteToMove == m_playerWhite)
            endGame(QStringLiteral("You ran out of time — %1 wins.").arg(opp));
        else
            endGame(QStringLiteral("%1 ran out of time — you win!").arg(opp));
    }
}

// ---------------------------------------------------------------------------
// Online play over delivery_module
//
// Two peers who share a game code meet on the content topic
// /logos-chess/1/game-<code>/json. The host beacons its presence every few
// seconds until a joiner answers; moves, chat, and game-end messages are small
// JSON payloads on the same topic, each tagged with the sender's session id so
// our own relayed messages can be discarded.

void ChessUiBackend::setOnline(const QString& state, const QString& info)
{
    setOnlineState(state);
    setOnlineInfo(info);
}

void ChessUiBackend::enterOnlineError(const QString& detail)
{
    m_beaconTimer.stop();
    setOnline(QStringLiteral("error"), detail);
}

void ChessUiBackend::appendChat(const QString& line)
{
    m_chatLines << line;
    while (m_chatLines.size() > 200)
        m_chatLines.removeFirst();
    setChatLog(m_chatLines.join(QLatin1Char('\n')));
}

void ChessUiBackend::ensureDelivery(std::function<void(bool, QString)> done)
{
    if (!m_eventsArmed) {
        m_eventsArmed = true;
        modules().delivery_module.onMessageReceived(
            [this](const QString&, const QString& contentTopic, QByteArray payload, int) {
                if (!m_topic.isEmpty() && contentTopic == m_topic)
                    handleDeliveryMessage(payload);
            });
    }
    if (m_deliveryReady) {
        done(true, QString());
        return;
    }
    // createNode rejects duplicates and start is not idempotent — another
    // consumer (Basecamp's Chat app) may already own the node. Both results
    // are deliberately ignored; subscribe is the call that has to succeed.
    const QString cfg = QStringLiteral(
        "{\"mode\":\"Core\",\"preset\":\"logos.dev\",\"logLevel\":\"ERROR\"}");
    modules().delivery_module.createNodeAsync(cfg, [this, done](LogosResult) {
        modules().delivery_module.startAsync([this, done](LogosResult) {
            m_deliveryReady = true;
            done(true, QString());
        });
    });
}

void ChessUiBackend::teardownOnline(bool notifyPeer)
{
    if (m_topic.isEmpty())
        return;
    if (notifyPeer && m_deliveryReady)
        publishJson({{QStringLiteral("t"), QStringLiteral("leave")}});
    if (m_deliveryReady)
        modules().delivery_module.unsubscribeAsync(m_topic, [](LogosResult) {});
    ++m_onlineGen;
    m_beaconTimer.stop();
    m_topic.clear();
    m_pendingRemoteMove.clear();
    m_isHost = false;
    m_drawOffered = false;
    m_peerOfferedDraw = false;
    setPeerName(QString());
    setGameCode(QString());
    setOnline(QStringLiteral("offline"), QString());
}

void ChessUiBackend::hostOnlineGame(QString code, bool asWhite, int timeControlMin, QString name)
{
    teardownOnline(true);
    static const QRegularExpression bad(QStringLiteral("[^a-z0-9-]"));
    const QString clean = code.trimmed().toLower().remove(bad).left(24);
    if (clean.isEmpty()) {
        setOnline(QStringLiteral("error"), QStringLiteral("Enter a game code first."));
        return;
    }
    m_selfName = name.trimmed().isEmpty() ? QStringLiteral("Host") : name.trimmed();
    m_playerWhite = asWhite;
    m_hostTcMin = qMax(0, timeControlMin);
    m_isHost = true;
    m_topic = QStringLiteral("/logos-chess/1/game-%1/json").arg(clean);
    setGameCode(clean);
    setOnline(QStringLiteral("starting"), QStringLiteral("Starting the delivery node..."));

    const int gen = ++m_onlineGen;
    ensureDelivery([this, gen](bool ok, QString detail) {
        if (gen != m_onlineGen)
            return;
        if (!ok) {
            enterOnlineError(detail);
            return;
        }
        subscribeTopic();
    });
    QTimer::singleShot(10000, &m_proc, [this, gen]() {
        if (gen == m_onlineGen && onlineState() == QLatin1String("starting"))
            enterOnlineError(QStringLiteral(
                "The delivery module is not responding — is delivery_module installed and loaded?"));
    });
}

void ChessUiBackend::joinOnlineGame(QString code, QString name)
{
    teardownOnline(true);
    static const QRegularExpression bad(QStringLiteral("[^a-z0-9-]"));
    const QString clean = code.trimmed().toLower().remove(bad).left(24);
    if (clean.isEmpty()) {
        setOnline(QStringLiteral("error"), QStringLiteral("Enter the game code you were given."));
        return;
    }
    m_selfName = name.trimmed().isEmpty() ? QStringLiteral("Guest") : name.trimmed();
    m_isHost = false;
    m_topic = QStringLiteral("/logos-chess/1/game-%1/json").arg(clean);
    setGameCode(clean);
    setOnline(QStringLiteral("starting"), QStringLiteral("Starting the delivery node..."));

    const int gen = ++m_onlineGen;
    ensureDelivery([this, gen](bool ok, QString detail) {
        if (gen != m_onlineGen)
            return;
        if (!ok) {
            enterOnlineError(detail);
            return;
        }
        subscribeTopic();
    });
    QTimer::singleShot(10000, &m_proc, [this, gen]() {
        if (gen == m_onlineGen && onlineState() == QLatin1String("starting"))
            enterOnlineError(QStringLiteral(
                "The delivery module is not responding — is delivery_module installed and loaded?"));
    });
}

void ChessUiBackend::leaveOnlineGame()
{
    const bool wasPlaying = onlineMode() && gameState() != QLatin1String("gameOver")
        && onlineState() == QLatin1String("playing");
    teardownOnline(true);
    if (wasPlaying) {
        ++m_gen;
        endGame(QStringLiteral("You left the game."));
    }
}

void ChessUiBackend::sendChat(QString text)
{
    const QString trimmed = text.trimmed().left(400);
    if (trimmed.isEmpty() || m_topic.isEmpty())
        return;
    publishJson({{QStringLiteral("t"), QStringLiteral("chat")},
                 {QStringLiteral("name"), m_selfName},
                 {QStringLiteral("text"), trimmed}});
    appendChat(QStringLiteral("You: %1").arg(trimmed));
}

void ChessUiBackend::subscribeTopic()
{
    const int gen = m_onlineGen;
    modules().delivery_module.subscribeAsync(m_topic, [this, gen](LogosResult r) {
        if (gen != m_onlineGen)
            return;
        if (!r.success) {
            enterOnlineError(QStringLiteral("Could not subscribe to the game topic: %1")
                                 .arg(r.getError()));
            return;
        }
        if (m_isHost) {
            setOnline(QStringLiteral("waiting"),
                      QStringLiteral("Waiting for an opponent — share the code \"%1\".")
                          .arg(gameCode()));
            sendBeacon();
            m_beaconTimer.start();
        } else {
            setOnline(QStringLiteral("joining"),
                      QStringLiteral("Looking for the host of \"%1\"...").arg(gameCode()));
        }
    });
}

void ChessUiBackend::sendBeacon()
{
    publishJson({{QStringLiteral("t"), QStringLiteral("host")},
                 {QStringLiteral("name"), m_selfName},
                 {QStringLiteral("color"), m_playerWhite ? QStringLiteral("w")
                                                         : QStringLiteral("b")},
                 {QStringLiteral("tcMin"), m_hostTcMin}});
}

void ChessUiBackend::publishJson(const QVariantMap& obj)
{
    if (m_topic.isEmpty())
        return;
    QVariantMap tagged = obj;
    tagged.insert(QStringLiteral("v"), 1);
    tagged.insert(QStringLiteral("from"), m_selfId);
    const QByteArray payload =
        QJsonDocument(QJsonObject::fromVariantMap(tagged)).toJson(QJsonDocument::Compact);
    modules().delivery_module.sendAsync(m_topic, payload, [](LogosResult) {});
}

void ChessUiBackend::handleDeliveryMessage(const QByteArray& payload)
{
    const QJsonObject msg = QJsonDocument::fromJson(payload).object();
    if (msg.isEmpty() || msg.value(QStringLiteral("from")).toString() == m_selfId)
        return;
    const QString t = msg.value(QStringLiteral("t")).toString();
    const QString state = onlineState();

    if (t == QLatin1String("host") && !m_isHost) {
        if (state == QLatin1String("joining")) {
            const QString hostName = msg.value(QStringLiteral("name")).toString();
            const bool hostWhite =
                msg.value(QStringLiteral("color")).toString() != QLatin1String("b");
            const int tcMin = msg.value(QStringLiteral("tcMin")).toInt(10);
            setPeerName(hostName.isEmpty() ? QStringLiteral("Host") : hostName);
            publishJson({{QStringLiteral("t"), QStringLiteral("join")},
                         {QStringLiteral("name"), m_selfName}});
            setOnline(QStringLiteral("playing"),
                      QStringLiteral("Connected to %1.").arg(peerName()));
            appendChat(QStringLiteral("· Connected to %1.").arg(peerName()));
            startMatch(QStringLiteral("online"), !hostWhite, m_skill,
                       tcMin <= 0 ? 0 : qint64(tcMin) * 60000);
        } else if (state == QLatin1String("playing")) {
            // Our join answer was lost — the host is still beaconing.
            publishJson({{QStringLiteral("t"), QStringLiteral("join")},
                         {QStringLiteral("name"), m_selfName}});
        }
        return;
    }

    if (t == QLatin1String("join") && m_isHost) {
        if (state == QLatin1String("waiting")) {
            m_beaconTimer.stop();
            const QString guest = msg.value(QStringLiteral("name")).toString();
            setPeerName(guest.isEmpty() ? QStringLiteral("Guest") : guest);
            setOnline(QStringLiteral("playing"),
                      QStringLiteral("Connected to %1.").arg(peerName()));
            appendChat(QStringLiteral("· %1 joined the game.").arg(peerName()));
            startMatch(QStringLiteral("online"), m_playerWhite, m_skill,
                       m_hostTcMin <= 0 ? 0 : qint64(m_hostTcMin) * 60000);
        }
        return;
    }

    if (state != QLatin1String("playing"))
        return;

    if (t == QLatin1String("move")) {
        const int ply = msg.value(QStringLiteral("ply")).toInt(-1);
        const QString uci = msg.value(QStringLiteral("uci")).toString();
        const qint64 clockMs =
            static_cast<qint64>(msg.value(QStringLiteral("clockMs")).toDouble(-1));
        if (ply < m_moves.size())
            return;  // duplicate or our own echo of an old ply
        if (ply > m_moves.size()) {
            enterOnlineError(QStringLiteral("The game went out of sync — please start a new one."));
            return;
        }
        if (clockMs >= 0 && !m_untimed) {
            if (m_playerWhite) {
                m_blackMs = clockMs;
                setClockBlackMs(int(clockMs));
            } else {
                m_whiteMs = clockMs;
                setClockWhiteMs(int(clockMs));
            }
        }
        if (gameState() == QLatin1String("opponentTurn") && m_collectedMoves.contains(uci))
            applyMove(uci);
        else
            m_pendingRemoteMove = uci;  // validated once the current refresh lands
        return;
    }

    if (t == QLatin1String("chat")) {
        const QString name = msg.value(QStringLiteral("name")).toString();
        appendChat(QStringLiteral("%1: %2")
                       .arg(name.isEmpty() ? QStringLiteral("Opponent") : name)
                       .arg(msg.value(QStringLiteral("text")).toString().left(400)));
        return;
    }

    if (t == QLatin1String("resign")) {
        ++m_gen;
        endGame(QStringLiteral("%1 resigned — you win!").arg(peerName()));
        appendChat(QStringLiteral("· %1 resigned.").arg(peerName()));
        return;
    }

    if (t == QLatin1String("drawOffer")) {
        m_peerOfferedDraw = true;
        appendChat(QStringLiteral("· %1 offers a draw — press \"Agree a draw\" to accept.")
                       .arg(peerName()));
        return;
    }

    if (t == QLatin1String("drawAccept")) {
        if (m_drawOffered) {
            ++m_gen;
            endGame(QStringLiteral("Draw agreed."));
        }
        return;
    }

    if (t == QLatin1String("leave")) {
        appendChat(QStringLiteral("· %1 left the game.").arg(peerName()));
        if (gameState() != QLatin1String("gameOver")) {
            ++m_gen;
            endGame(QStringLiteral("%1 left the game.").arg(peerName()));
        }
        m_beaconTimer.stop();
        setOnline(QStringLiteral("offline"),
                  QStringLiteral("%1 left the game.").arg(peerName()));
        return;
    }
}
