# Scratch2Arduino roadmap

Phased plan from the empty repository to a release. Each phase has a goal, deliverables and "done when" criteria.

## Status

| Phase | Status |
|---|---|
| 0 — Setup and tooling | Done |
| 1 — Core foundations | Done |
| 2 — Arduino toolchain | Done (compile checked in CI on the 3 OS; upload to be checked on real boards) |
| 3a — Variant A MVP (webview) | Done (built and tested in CI on the 3 OS; upload to be checked on real boards) |
| 3b — Variant B MVP (Qt native) | Dropped (built in v0.3.0, removed in favour of the webview application) |
| 4 — Validation and friendly errors | Done: 10 rules with fr/en messages and hints (blocks not attached to a start block are greyed like comments, not reported), faulty blocks highlighted, compiler errors mapped to blocks |
| 5 — Live mode and real-time debugging | Done: firmware, protocol, interpreter, Live button (running scripts glow, variables and sensors with graphs), serial monitor and "write to the serial monitor" block; to be checked on real boards |
| 6 — Sensors and actuators | Done, to be checked on real hardware: every listed component has blocks in upload and Live modes, with automatic library installation; example projects with wiring diagrams (servo-potentiometer, distance-alarm, thermometer, reaction-game, light-gauge) |
| 7 — Productivity and sharing | Done: "My blocks" library (right-click a block to save it with the blocks below, palette category to add groups back, rename/delete, export/import as `.s2ablocks`); example gallery with wiring diagrams on New (blank project as template); recent projects menu; autosave every 15 s with recovery at start-up |
| 8 → 11 | Not started |
| 12 — Variant comparison | Dropped (only the webview application is kept) |

Branches: `main` holds released states, `dev` integrates the work, each feature lives on its own
`feature/…` (or `fix/…`, `docs/…`) branch merged into `dev` through a pull request once CI is green.

## Architecture decisions

- **One application: `apps/webview`** — native C++ host embedding a system webview (library `webview/webview`, CEF as optional fallback). The UI (`frontend/`) is built from `scratch-blocks` + the `tools-react-components` theme and bundled **offline** inside the app (no server, no browser).
  - Decision of 2026-11-05: the Qt 6 native variant (B, `apps/native`, custom block canvas), built up to v0.3.0, is dropped to keep a single editor with the style of `tools-react-components`. Phases 3b and 12 are closed accordingly.
- **`core/` — `s2a_core` (C++17 static library)**, holding all the logic (tested without a GUI):
  - project model and `.s2a` format (zip: `project.json` + assets + format version) → export / re-import;
  - data-driven block catalog (`blocks/*.json`, fr/en labels) — converted to Blockly JSON for the editor;
  - validator run **only** on Build/Run, returning kid-friendly localized errors tied to block IDs (the editor never raises errors);
  - code generator (block tree → Arduino C++ `.ino`);
  - toolchain wrapper around a bundled `arduino-cli` (board detection, compile, upload, library install);
  - board database (Uno, Nano, Mega, then ESP32 / ESP8266);
  - transports: USB serial, Bluetooth (HC-05/HC-06, transparent serial), Wi-Fi (ESP, TCP/WebSocket);
  - live protocol talking to the *S2A live* firmware (`firmware/`, Firmata-like) for real-time control and debugging;
  - "My blocks" user library (saved block groups, no new block types);
  - i18n: French (default) and English.
- **Text languages**: read-only view of generated C++ first, then Python (embedded `pocketpy`) and JavaScript (embedded `QuickJS`) driving the board through the live protocol.
- **Install**: Windows `Scratch2Arduino_installer.exe` (Inno Setup or NSIS, `installer/windows/`) that installs the missing prerequisites and runs CMake configure/build/install, updating an existing installation incrementally (only changed sources are rebuilt); Linux/macOS `Scratch2Arduino_installer.sh` + top-level `Makefile` (`make`, `make install`, `make uninstall`) wrapping CMake.
- Project overview and layout: [README](../README.md).

## Roadmap

Each phase lists its **goal**, **deliverables** and **done when** criteria.

### Phase 0 — Setup and tooling
- **Goal**: an empty but buildable project on all three OS.
- **Deliverables**: `git init` and the planned layout; top-level `CMakeLists.txt` with option `S2A_BUILD_WEBVIEW`, `CMakePresets.json`; first `Scratch2Arduino_installer.sh`, `Makefile` and `installer/windows` script; CI (GitHub Actions) on Windows, Linux, macOS; `.clang-format`, `.clang-tidy`, `.editorconfig`; license; CONTRIBUTING draft.
- **Done when**: CI builds a "hello window" for each variant on the three OS, and `./Scratch2Arduino_installer.sh` / `make install` / `Scratch2Arduino_installer.exe` each install it.

### Phase 1 — Core foundations
- **Goal**: a shared, tested data model.
- **Deliverables**: project model and `.s2a` read/write with format versioning; JSON schema for the block catalog; board database (Uno, Nano, Mega: pins, PWM, analog, interrupts); i18n loader (fr default, en); GoogleTest suite.
- **Done when**: a project can be saved, reloaded and compared byte-for-byte in tests; strings switch between fr and en.

### Phase 2 — Arduino toolchain
- **Goal**: compile and upload without the Arduino IDE.
- **Deliverables**: bundled `arduino-cli` (downloaded at build time per OS); port and board detection; compile + upload of a hand-written sketch; parser turning compiler/upload errors into readable messages.
- **Done when**: Blink is uploaded to Uno, Nano and Mega from a core CLI test tool on all three OS.

### Phase 3a — Variant A MVP (webview)
- **Goal**: first usable app with scratch-blocks.
- **Deliverables**: webview host; `frontend/` (pnpm + Vite/webpack) embedding `scratch-blocks` with the `tools-react-components` theme; JS ↔ C++ bridge (project, build, serial); basic blocks (events, digital/analog write & read, wait, repeat/forever, if/else, variables, math, logic); save / open; board and port picker; Build and Upload buttons; code generator for these blocks.
- **Done when**: a beginner builds Blink with blocks and uploads it; the project reopens identically.

### Phase 3b — Variant B MVP (Qt native) — dropped
- **Dropped on 2026-11-05** (done in v0.3.0, then removed: only the webview application is kept).
- **Goal**: same MVP in pure C++/Qt.
- **Deliverables**: Qt window; block canvas (render, drag-and-drop, snapping, palette, zoom/scroll); same block catalog and code generator from `core/`; save / open the same `.s2a` files; Build and Upload.
- **Done when**: a project created in A opens, edits and uploads in B, and the other way round.

### Phase 4 — Validation and friendly errors
- **Goal**: errors only at Build/Run, clear and helpful.
- **Deliverables**: validator rules (missing inputs, unused/empty scripts, invalid pin for board, type mismatches…); fr/en child-friendly messages with "how to fix" hints; highlighting and focusing of the faulty block in both variants; compiler errors mapped back to blocks where possible.
- **Done when**: every validator rule has a test, and no error or popup appears while editing.

### Phase 5 — Live mode and real-time debugging
- **Goal**: instant feedback with the board connected.
- **Deliverables**: *S2A live* firmware (`firmware/`) and its protocol; one-click "Live" mode flashing it if needed; host-side interpreter running blocks against the board; running-block highlighting; live sensor/variable panel and graph; serial monitor.
- **Done when**: changing a block while in Live mode changes the board behaviour without re-uploading.

### Phase 6 — Sensors and actuators
- **Goal**: cover the usual beginner kits.
- **Deliverables**: block sets with wiring help images for LEDs, RGB LED, buttons, potentiometer, light sensor, buzzer, servo, DC motor (driver), ultrasonic HC-SR04, DHT11/22, LCD 16×2 (I2C), OLED SSD1306, NeoPixel, LED matrix; automatic Arduino library installation via `arduino-cli`; support in both upload and live modes.
- **Done when**: each component has at least one example project tested on real hardware.

### Phase 7 — Productivity and sharing
- **Goal**: reuse and learn from others.
- **Deliverables**: "My blocks" library (save a selection, reuse, export/import); example project gallery; project export / import (`.s2a`) and templates; autosave and recent projects.
- **Done when**: a saved block group can be reused in another project and survives export/import.

### Phase 8 — Wireless
- **Goal**: control boards without a cable.
- **Deliverables**: Bluetooth transport (HC-05/HC-06); ESP32 / ESP8266 boards in the database; Wi-Fi transport for live mode; pairing / connection UI.
- **Done when**: live mode works over Bluetooth and Wi-Fi with the same blocks as over USB.

### Phase 9 — Text programming languages
- **Goal**: a path from blocks to text code.
- **Deliverables**: read-only Arduino C++ view synced with blocks; Python mode (embedded `pocketpy`) and JavaScript mode (embedded `QuickJS`) with an `arduino` API over the live protocol; editor with syntax highlighting.
- **Done when**: the same Blink program can be written in blocks, Python and JavaScript.

### Phase 10 — Documentation and education
- **Goal**: self-service learning for students, teachers and workshops.
- **Deliverables** (fr and en): user guide; step-by-step tutorials following the learner progression below; teacher / workshop kits (lesson plans, hardware lists); contributor documentation.
- **Done when**: a newcomer completes the first tutorial without outside help.

### Phase 11 — Packaging and release
- **Goal**: one-click installation everywhere.
- **Deliverables**: final Windows `Scratch2Arduino_installer.exe` (prerequisite check/installation, CMake build & install, shortcuts, uninstaller, signed); final `Scratch2Arduino_installer.sh` (dependency install via apt / dnf / pacman / brew) and `Makefile` (`make`, `make install`, `make uninstall`); macOS bundle; update check; release process and changelog; CONTRIBUTING, issue templates.
- **Done when**: CI produces release artifacts for the three OS, installed and smoke-tested on clean machines.

### Phase 12 — Variant comparison — dropped
- **Dropped on 2026-11-05**: decision taken early, only the webview application is kept.
- **Goal**: decide the long-term direction.
- **Deliverables**: comparison of A and B once both finish Phase 6 — tests with children, performance, binary size, maintainability, contributor friendliness.
- **Done when**: a written decision (keep both, or focus on one).

## Learner progression (example projects)

1. **Blink** — digital output, wait, forever.
2. **Traffic light** — sequences, several LEDs.
3. **Button and LED** — digital input, if/else.
4. **Potentiometer and servo** — analog input, mapping values.
5. **Distance alarm** — ultrasonic sensor, buzzer, thresholds.
6. **Thermometer display** — DHT sensor, LCD/OLED, variables.
7. **Reaction game** — LED matrix, buzzer, buttons, score, random.
8. **Remote robot** — DC motors controlled over Bluetooth or Wi-Fi.

## Resources

- [scratch-blocks](https://github.com/scratchfoundation/scratch-blocks) (Blockly 12, TypeScript) — block editor.
- `@epitech/tools-react-components` (React + Mantine, fr/en locales) — design system.
- [arduino-cli](https://github.com/arduino/arduino-cli) — compile, upload, board and library management.
- [Firmata](https://github.com/firmata/protocol) — inspiration for the live protocol.
- [webview/webview](https://github.com/webview/webview) — native webview for the application; [CEF](https://bitbucket.org/chromiumembedded/cef) as fallback.
- [pocketpy](https://github.com/pocketpy/pocketpy), [QuickJS](https://bellard.org/quickjs/) — embedded Python and JavaScript.
- [GoogleTest](https://github.com/google/googletest) — C++ unit tests.
- [Inno Setup](https://jrsoftware.org/isinfo.php) / [NSIS](https://nsis.sourceforge.io/) — Windows installer.
