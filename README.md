# Chess for Logos Basecamp

Play chess against Stockfish, inside [Logos Basecamp](https://docs.logos.co/basecamp).

Chess is a `ui_qml` Logos module: a QML board rendered by Basecamp, with a
process-isolated C++ backend that drives the [Stockfish](https://stockfishchess.org/)
engine over UCI. The engine is also the referee: legal moves come from Stockfish's
`go perft 1`, board state and check detection from its `d` command, so the module
ships no hand-written move generator that could disagree with the rules.

## What it does

Three ways to play, switchable from the top-left mode tabs:

- **Engine** — vs Stockfish: difficulty 0 to 20 (adjustable mid-game from
  Settings), live clocks, evaluation bar and readout, engine commentary stream,
  full-strength hints, undo, typed SAN/UCI move input
- **Table** — two humans passing one machine: editable names, both clocks live,
  material tracker, resign and agree-a-draw, board turns after every move
- **Online** — two humans on different machines over
  [Logos Delivery](https://github.com/logos-co/logos-delivery-module), World
  Chess Network style: share a game code, host and join, play in real time
  with chat; moves and messages travel peer to peer on a content topic
  (`/logos-chess/1/game-<code>/json`), no game server anywhere

Everywhere: click-to-move with legal-move markers, last-move / check / hint
highlights, SAN move list, promotion picker, and full game-end detection
(checkmate, stalemate, fifty-move rule, threefold repetition, insufficient
material, time forfeit). Stockfish referees all modes, and engine
auto-discovery has an in-app path override in Settings.

Online mode needs the `delivery_module` loaded in Basecamp (bundled
automatically in the dev runner). Note that game topics are unencrypted: use
a hard-to-guess code and keep the trash talk friendly.

## What is Logos Basecamp?

[Logos Basecamp](https://docs.logos.co/basecamp) is the desktop shell for
[Logos](https://logos.co) — it bundles the kernel, the default modules, and the
UI packages into one surface, so you can discover, install, and run Logos
modules and apps from a graphical interface instead of the command line. Chess
is one such app: it installs into Basecamp and runs inside it.

**Get Basecamp:** download the latest release from
[logos-co/logos-basecamp/releases/latest](https://github.com/logos-co/logos-basecamp/releases/latest).
Releases are published as:

| File | Platform |
|---|---|
| `LogosBasecamp-Desktop-v<ver>-<sha>-aarch64.dmg` | macOS Apple Silicon |
| `LogosBasecamp-Desktop-v<ver>-<sha>-x86_64.AppImage` | Linux x86_64 |
| `LogosBasecamp-Desktop-v<ver>-<sha>-aarch64.AppImage` | Linux ARM64 |

On Linux, `chmod +x` the AppImage and run it — no installation needed. There is
currently no macOS Intel build, so Intel Macs need to build Basecamp from
source (it builds with Nix: `nix build '.#bin-appimage'`, see the
[Basecamp repo](https://github.com/logos-co/logos-basecamp)).

Further reading: [Basecamp docs](https://docs.logos.co/basecamp) ·
[Get started with Logos](https://logos.co/get-started) ·
[Logos on GitHub](https://github.com/logos-co)

This app is developed against Basecamp 0.2.x (verified on 0.2.0-RC3). Online
mode also needs the `delivery_module`, which ships with Basecamp's default
module set — check **Modules** if you are unsure.

## Install

### 1. Install Stockfish

The chess engine is not bundled. Get an official build:

```bash
# macOS
brew install stockfish

# Debian / Ubuntu
sudo apt install stockfish
```

Or download from the
[official releases](https://official-stockfish.github.io/docs/stockfish-wiki/Download-and-usage.html).

### 2. Install the module

Download the `.lgx` for your platform from the
[latest release](../../releases/latest):

| Asset | Platform | Variant inside |
|---|---|---|
| `chess_ui-darwin-arm64.lgx` | macOS Apple Silicon | `darwin-arm64` |
| `chess_ui-linux-x86_64.lgx` | Linux x86_64 (Debian, Ubuntu, …) | `linux-amd64` |
| `chess_ui-linux-aarch64.lgx` | Linux ARM64 | `linux-arm64` |

Then in Basecamp open **Modules**, click **Install LGX Package**, select the
file, and **Load** it. Chess appears with the pawn icon. This is the
recommended route on every platform — it puts the plugin where Basecamp
actually looks, with no path guessing.

CLI alternative — note Basecamp resolves its data directory via Qt's
`AppDataLocation`, so the base differs per platform (and a non-portable dev
build appends `Dev`):

```bash
# macOS
BASECAMP_DIR="$HOME/Library/Application Support/Logos/LogosBasecamp"
# Linux
# BASECAMP_DIR="$HOME/.local/share/Logos/LogosBasecamp"

lgpm --modules-dir "$BASECAMP_DIR/modules" install --file chess_ui-linux-x86_64.lgx
```

If Basecamp was launched with `--user-dir` (or `LOGOS_USER_DIR`), use that
path as `BASECAMP_DIR` instead.

Release assets are portable builds (self-contained, bundled support libraries).
If your platform has no prebuilt asset yet, build from source below.

## Build from source

Requires Nix with flakes ([install](https://nixos.org/download/)). Stockfish is
pulled in automatically for Nix builds.

```bash
git clone https://github.com/IP3-Studio/Basecamp-chess-app
cd Basecamp-chess-app
nix build                      # compile the plugin
nix run .                      # play it in logos-standalone-app
nix build .#lgx-portable       # package a shareable .lgx for your platform

DEV_QML_PATH=$PWD/src/qml nix run .   # live-reload QML while hacking
nix build .#integration-test -L       # run the UI tests
```

## How the engine is located

First match wins:

1. Path saved from the in-app "Use engine path" field (QSettings `Logos/chess_ui`)
2. `LOGOS_CHESS_STOCKFISH` environment variable
3. `stockfish` on `PATH`
4. Path baked in at build time (the Nix store path, for Nix-built installs)
5. Common locations: `/opt/homebrew/bin`, `/usr/local/bin`, `/usr/bin`,
   `/usr/games`, `~/.nix-profile/bin`, `/run/current-system/sw/bin`

If nothing is found, the app shows install guidance and a path field.

## How it works

```
Basecamp (or logos-standalone-app)
  ├─ renders src/qml/Main.qml (the board)
  └─ spawns ui-host with chess_ui_plugin
        └─ ChessUiBackend ── UCI over stdin/stdout ──> stockfish
             position/d ......... board state, FEN, checkers
             go perft 1 ......... legal move list
             go movetime N ...... engine moves and hints
```

QML and backend talk over Qt Remote Objects; the contract lives in
[src/chess_ui.rep](src/chess_ui.rep). Engine replies are matched to a FIFO
queue of pending requests, with generation counters so a new game invalidates
in-flight replies. A backend crash cannot take Basecamp down.

## Layout

```
├── metadata.json                  # module manifest (type: ui_qml, universal)
├── flake.nix                      # builds via logos-module-builder
├── src/
│   ├── chess_ui.rep               # QtRO view contract
│   ├── chess_ui_backend.{h,cpp}   # UCI driver + game state (the only C++)
│   └── qml/Main.qml               # board UI
├── icons/chess.png                # generated by scripts/make_icon.py
└── tests/ui-tests.mjs             # integration tests
```

## Known limitations

- Move list uses UCI notation (e2e4), not SAN; no PGN export yet
- Threefold repetition is declared automatically at the third occurrence
  (no claim step), and only the simple insufficient-material cases
  (K vs K, K+B vs K, K+N vs K) are recognised

## License

Licensed under either of [Apache License 2.0](LICENSE-APACHE) or
[MIT license](LICENSE-MIT) at your option, matching the Logos ecosystem
convention.

Stockfish is a separate GPLv3 program. This module does not bundle or link it;
it runs your locally installed copy as a subprocess and talks UCI over
stdin/stdout.
