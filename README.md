# Scratch2Arduino

> Program real Arduino boards with Scratch-style blocks — no coding experience required.

**Status: pre-alpha — Phase 2 done (Arduino toolchain).** Both variants build and open a welcome window; the shared core knows the boards, the blocks and the `.s2a` project format, and compiles and uploads Arduino programs through the bundled `arduino-cli` (try it with `s2a-cli`). The block editor is not there yet. See the [roadmap](docs/roadmap.md).

---

## Overview

Scratch2Arduino is a cross-platform desktop application (Windows, Linux, macOS) that lets young people and beginners build interactive electronics projects by snapping together visual code blocks, just like in Scratch. Projects are compiled and uploaded to an Arduino board in one click, or driven live from the computer for instant feedback.

It bridges the gap between visual programming and physical computing: LEDs blink, motors spin and sensors report values as soon as the blocks are placed.

### Goals

- **Accessible**: designed for children, classrooms and workshops; French by default, English available.
- **Forgiving**: the editor never shows errors while you build. Problems are reported only when you click **Build** or **Run**, with clear, friendly messages that point to the block involved.
- **Native and offline**: a C/C++ application built with CMake, like the Arduino IDE. No website, no account, no internet needed once installed.
- **Open**: community-driven, with contributions, bug reports and feature requests welcome.

## Features

| Area | What you get |
|---|---|
| Programming | Drag-and-drop blocks; read-only view of the generated Arduino C++; Python and JavaScript modes (later) |
| Boards | Arduino Uno, Nano, Mega first; ESP32 / ESP8266 later |
| Hardware | LEDs, buttons, potentiometers, servos, DC motors, ultrasonic & temperature sensors, LCD/OLED displays, NeoPixels, buzzers… |
| Live mode | Control the board in real time, watch sensor values, see which block is running |
| Debugging | Serial monitor, live variable/sensor view, block highlighting, friendly build errors |
| Wireless | Bluetooth (HC-05/HC-06) and Wi-Fi (ESP boards) |
| Reuse | Save groups of blocks to **My blocks** and reuse them in any project (new block types cannot be created) |
| Projects | Save, export and re-import projects as a single `.s2a` file; gallery of example projects |
| Libraries | Access to the Arduino library ecosystem through `arduino-cli` |
| Languages | French (default) and English, more to come |

## Two variants

Scratch2Arduino is developed as **two applications that share the same core** and open the same project files:

| | **A — `webview`** | **B — `native`** |
|---|---|---|
| UI technology | C++ host + system webview (bundled offline) | Qt 6 (Widgets/QML), pure C++ |
| Block editor | [scratch-blocks](https://github.com/scratchfoundation/scratch-blocks) (Blockly 12) | Custom Qt block canvas |
| Design | `@epitech/tools-react-components` theme (React + Mantine) | Qt styling matching the same palette |
| Strengths | Reuses the familiar Scratch look and a mature editor | Single technology stack, no web runtime |
| Trade-offs | JS ↔ C++ bridge, frontend build with Node/pnpm | Block editor written from scratch |

Both variants will be compared once they reach the same feature level (see roadmap, Phase 12) to decide whether to keep both.

## Architecture

```
            ┌───────────────────────┐   ┌───────────────────────┐
            │ apps/webview (A)      │   │ apps/native (B)       │
            │ webview + scratch-    │   │ Qt 6 block canvas     │
            │ blocks + React UI     │   │                       │
            └──────────┬────────────┘   └──────────┬────────────┘
                       └─────────────┬─────────────┘
                          ┌──────────▼──────────┐
                          │  core/ (s2a_core)   │
                          │  project model .s2a │
                          │  block catalog      │
                          │  validator          │
                          │  code generator     │
                          │  arduino-cli wrapper│
                          │  serial / BT / Wi-Fi│
                          │  live protocol      │
                          │  i18n (fr, en)      │
                          └──────────┬──────────┘
                                     │ USB / Bluetooth / Wi-Fi
                          ┌──────────▼──────────┐
                          │ Arduino board       │
                          │ user sketch, or     │
                          │ S2A live firmware   │
                          └─────────────────────┘
```

- **Upload mode**: blocks → validated → Arduino C++ → compiled and uploaded with the bundled `arduino-cli`. The board runs on its own.
- **Live mode**: a small *S2A live* firmware (Firmata-like) is flashed once; the computer then drives the pins in real time and streams sensor values back.

## Repository layout

```
Scratch2Arduino/
├── CMakeLists.txt          # top-level build, options S2A_BUILD_WEBVIEW / S2A_BUILD_NATIVE
├── CMakePresets.json
├── Makefile                # Linux/macOS convenience wrapper around CMake
├── install.sh              # Linux/macOS one-command installer
├── cmake/                  # CMake modules and toolchain helpers
├── core/                   # s2a_core: shared C++17 library + unit tests
├── blocks/                 # block catalog (JSON, fr/en labels) and its JSON schema
├── apps/
│   ├── webview/            # variant A: C++ host embedding the web UI
│   └── native/             # variant B: Qt 6 application
├── frontend/               # (Phase 3a) variant A UI: scratch-blocks + React (built with pnpm)
├── firmware/               # (Phase 5) S2A live firmware for Arduino / ESP boards
├── tools/s2a-cli/          # command-line access to the toolchain
├── installer/
│   └── windows/            # installer.exe script (Inno Setup / NSIS)
├── resources/              # boards, translations (fr/en), icons, Linux .desktop template
├── examples/               # sketches (Blink); example projects (.s2a) in Phase 7
└── docs/                   # roadmap, block catalog guide; later user guide, tutorials
```

## Getting started

### Windows

Download **`installer.exe`** (built by CI, artifact `installer-windows`) and run it. It checks for (and installs if needed) CMake and a C++ toolchain, builds and installs Scratch2Arduino with CMake, and adds Start-menu and desktop shortcuts plus an uninstaller.

### Linux (and macOS)

```bash
./install.sh            # installs missing dependencies (apt / dnf / pacman / brew), builds and installs
./install.sh --user     # same, for the current user only (~/.local)
./install.sh --uninstall

# or, with the dependencies already installed:
make && sudo make install
sudo make uninstall
```

### Manual build

Prerequisites:

- CMake ≥ 3.25 and a C++17 compiler (GCC, Clang or MSVC)
- Variant A: WebKitGTK 4.1 + pkg-config on Linux (WebKit on macOS and WebView2 on Windows are built in)
- Variant B: Qt 6 (Widgets)
- Later phases: Node.js + pnpm (variant A frontend), `arduino-cli` (downloaded by the build)

```bash
cmake --preset release                 # add -DS2A_BUILD_NATIVE=OFF or -DS2A_BUILD_WEBVIEW=OFF to build only one variant
cmake --build --preset release
ctest --preset release
cmake --install build/release

cmake --preset core-only && cmake --build --preset core-only   # core + tests, no GUI dependencies
```

### Check the toolchain with `s2a-cli`

`s2a-cli` is installed next to the applications. It uses the same bundled `arduino-cli` and the same
friendly messages, and is handy to check a classroom computer before a workshop:

```bash
s2a-cli boards                                        # supported boards
s2a-cli ports                                         # plugged-in boards (port and board name)
s2a-cli setup                                         # download the board tools once (Internet needed)
s2a-cli compile examples/sketches/blink --board uno
s2a-cli upload examples/sketches/blink --board uno --port /dev/ttyACM0   # COM3 on Windows
```

Messages are in the system language; `--lang en` or `--lang fr` forces one, `--verbose` shows the raw tool output.
Scratch2Arduino keeps its own copy of the Arduino cores and libraries (in `~/.local/share/Scratch2Arduino`,
`~/Library/Application Support/Scratch2Arduino` or `%LOCALAPPDATA%\Scratch2Arduino`), so it never changes an
Arduino IDE installed on the same computer.

On Linux, uploading needs access to the serial port: `sudo usermod -aG dialout $USER`, then log out and back in.

### First project *(Phase 3)*

1. Plug in an Arduino Uno with a USB cable.
2. Start Scratch2Arduino and select your board and port.
3. Drag **when program starts**, **forever**, **turn LED on pin 13 on**, **wait 1 second**, **turn LED on pin 13 off**, **wait 1 second**.
4. Click **Upload**. The on-board LED blinks!

## Project files

A project is a single **`.s2a`** file (a zip archive) containing `project.json` (blocks, board, variables, format version) and any assets under `assets/`. It can be exported, shared and re-imported in either variant. Saving the same project twice gives identical bytes, and newer format versions are refused with a clear message instead of being misread. Saved **My blocks** will live in the user's library folder and be exported in the same way.

```json
{
  "format": "scratch2arduino-project",
  "formatVersion": 1,
  "name": "Clignotant",
  "board": "uno",
  "variables": [],
  "scripts": [
    { "x": 40, "y": 40, "blocks": [
      { "id": "a1", "type": "events_when_program_starts" },
      { "id": "a2", "type": "control_forever", "statements": { "SUBSTACK": [
        { "id": "a3", "type": "pins_builtin_led", "fields": { "STATE": "ON" } },
        { "id": "a4", "type": "control_wait", "inputs": { "SECONDS": { "value": "1" } } }
      ] } }
    ] }
  ]
}
```

## Boards and blocks

- Boards are described in [`resources/boards/`](resources/boards) (pins and their capabilities, buses, `arduino-cli` FQBN, USB ids): Arduino Uno, Nano and Mega 2560.
- Blocks are described in [`blocks/catalog/`](blocks/catalog): see the [block catalog guide](docs/block-catalog.md).
- Interface texts are in [`resources/i18n/`](resources/i18n) (`fr.json`, `en.json`).

## Languages

The interface is available in **French (default)** and **English**. Translations live in JSON files, and contributions for other languages are welcome.

## Contributing

Bug reports, feature requests, translations, example projects and code are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md) and the [roadmap](docs/roadmap.md).

## License

[MIT](LICENSE)

## Acknowledgements

Built on [scratch-blocks](https://github.com/scratchfoundation/scratch-blocks) / [Blockly](https://github.com/RaspberryPiFoundation/blockly), [arduino-cli](https://github.com/arduino/arduino-cli), [webview](https://github.com/webview/webview) and [Qt](https://www.qt.io/).
