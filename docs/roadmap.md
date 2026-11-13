# Scratch2Arduino roadmap

Phased plan from the empty repository to a release. Each phase has a goal, deliverables and "done when" criteria.

## Status

| Phase | Status |
|---|---|
| 0 - Setup and tooling | Done |
| 1 - Core foundations | Done |
| 2 - Arduino toolchain | Done (compile checked in CI on the 3 OS; upload to be checked on real boards) |
| 3a - Variant A MVP (webview) | Done (built and tested in CI on the 3 OS; upload to be checked on real boards) |
| 3b - Variant B MVP (Qt native) | Dropped (built in v0.3.0, removed in favour of the webview application) |
| 4 - Validation and friendly errors | Done: 10 rules with fr/en messages and hints (blocks not attached to a start block are greyed like comments, not reported), faulty blocks highlighted, compiler errors mapped to blocks |
| 5 - Live mode and real-time debugging | Done: firmware, protocol, interpreter, Live button (running scripts glow, variables and sensors with graphs), serial monitor and "write to the serial monitor" block; to be checked on real boards |
| 6 - Sensors and actuators | Done, to be checked on real hardware: every listed component has blocks in upload and Live modes, with automatic library installation; example projects with wiring diagrams (servo-potentiometer, distance-alarm, thermometer, reaction-game, light-gauge) |
| 7 - Productivity and sharing | Done: "My blocks" library (right-click a block to save it with the blocks below, palette category to add groups back, rename/delete, export/import as `.s2ablocks`); example gallery with wiring diagrams on New (blank project as template); recent projects menu; autosave every 15 s with recovery at start-up |
| 8 - Wireless | Done, to be checked on real hardware: ESP32 board (USB upload and Live); Live mode over Wi-Fi (the board's own network or the home network, set up from the editor) and over Bluetooth (board search, macOS pairing); examples ESP32 dimmer and wireless weather station (Wi-Fi / Bluetooth); Phone blocks (Bluetooth terminal app) and the phone-lamp example mixing Wi-Fi and Bluetooth; ESP8266 (NodeMCU) board with Live mode over Wi-Fi; Live mode over an HC-05 / HC-06 module on Uno, Nano and Mega (pins saved on the board from the Bluetooth window, blocks kept off them); Phone blocks through the module ("Bluetooth module" block, upload and Live, wiring diagram), remote robot example in a Uno + HC-05 version |
| 9 - Text programming languages | Done - official feature since 2026-11-11 (validated by the user; requested on 2026-11-10 with a C/C++ mode): project format 2 (`language`, `code`); C++ projects compiled and uploaded as typed, compiler errors on their lines with friendly fr/en explanations; Python (pocketpy) and JavaScript (QuickJS-ng) run in Live mode with an Arduino API, Stop interrupts endless loops; CodeMirror 6 code editor with errors shown after Check/Upload/Live only. Blink works in blocks, C++, Python and JavaScript (core tests with a fake board). Main way in: a **"My code" file beside the blocks** (C++, Python or JavaScript) whose functions and variables get their own block category, with Blocks / Code views and light autocompletion; the whole-program text modes stay as an expert mode. Code converted between C++, Python and JavaScript when the language changes (core `convert`, `s2a-cli convert`; what it cannot translate is kept as comments with its lines). To check on real boards |
| 10 - Documentation and education | In progress: examples for every step of the learner progression (Button and LED, Remote robot added); novice / advanced levels of the eight examples of the progression (blocks only, or with "My code" in C++ / Python), shown as variants of one gallery card; user guide and 8 step-by-step tutorials in French and English (`docs/fr`, `docs/en`); teacher / workshop kit (room preparation, parts list, formats, one plan per session, assessment) in both languages; developer guide (architecture and recipes, in English like all developer docs, in the repository only: `docs/developer-guide.md`, not shown in the editor). tutorials and guide inside the editor ("Tutorials and help" panel beside the blocks, opened at the first start, "Open the example" button, "Step-by-step tutorial" in the gallery). Done once a newcomer completes the first tutorial without outside help (to check in a workshop) |
| 11 - Packaging and release | Dropped for now (maybe later) |
| 12 - Variant comparison | Dropped (only the webview application is kept) |
| 13 - Automatic wiring diagram | Done: part 1 - components and pins computed from the blocks (`resources/components.json`, core `wiring::plan`, `s2a-cli wiring`), warnings (pin used twice, I2C pins taken, USB power), "Wiring" window for every project; diagrams can be zoomed. Part 2 - diagram drawn by the core (SVG) for every project and board (Uno, Nano, Mega, ESP32, ESP8266): board headers as printed, numbered component cards, + / GND rails, one routed wire per leg with its resistor, PNG export. Decision (user, 2026-11-10): examples keep their hand-drawn breadboard pictures (shown first), projects made from scratch use the generated diagram. Done |

## Remaining work (2026-11-11)

Everything the phases plan is built. What is left:

### Checks on real hardware

Tested by the user on an Arduino Uno (Windows): upload, Live mode over USB, LEDs, buttons, passive buzzer, servo and
potentiometer, HC-SR04, DHT11, 16-pin LCD, MAX7219 matrix, and the examples Blink, Traffic light, Potentiometer and
servo, Distance alarm, Thermometer (16-pin LCD), Reaction game. Also tested on an Arduino Mega 2560. The README keeps the up-to-date list
("Tested on real hardware"). Still to run on a real board:

- Nano; Linux and macOS with a real board;
- NeoPixel strip (Light gauge), I2C LCD, OLED, RGB LED, DC motors (L298N), light sensor;
- ESP32: USB upload, Live over Wi-Fi (board network and home network) and over Bluetooth, macOS pairing;
- ESP8266 (NodeMCU): Live over Wi-Fi;
- HC-05 / HC-06 on Uno, Nano and Mega: pins saved from the Bluetooth window, Live mode through the module, "Bluetooth module" block in upload and Live, the Uno + HC-05 remote robot (no module at hand yet);
- the examples added after the first hardware tests (Button and LED, Joystick and LEDs, Motion alarm, Obstacle detector, Remote robot, Phone lamp, Wireless station, ESP32 dimmer, advanced variants) and their components (joystick, PIR, infrared obstacle sensor);
- functions with inputs on a board;
- step-by-step Live mode and the Live wiring check;
- Python and JavaScript in Live mode, and C++ projects, on a real board (Phase 9).

### Phase 10: workshop

- Run a workshop (or a test with a newcomer) on the first tutorial to close the phase.
- Fix the guide, tutorials and teacher kit from what the workshop shows.

### Known limits

- On AVR boards (Uno, Nano, Mega) the Live firmware reads lines of 48 characters at most: long "send to the phone" texts are cut in Live mode (uploaded programs are not affected).
- The CI fallback for the apt mirror (`.github/actions/setup-apt`, switch to `archive.ubuntu.com` when the Azure mirror does not answer) has not run for real yet: check it the next time the Azure mirror fails.

### Next features (to choose)

Goals of the project not yet covered, in the suggested order:

1. **Step-by-step mode in Live mode** ("real-time debugging"): done on 2026-11-12 (chosen by the user), for 0.12.0. Pause (F9) and "Next block" (F10) buttons in Live mode: the block that runs next is lit, each click runs one block (scripts take turns, waits are skipped), the variables and pins panel follows; block programs, with or without "My code". To check on a real board.
2. **Arduino library manager** ("extensive library support"): done on 2026-11-12 (chosen by the user), for 0.11.0. "Arduino libraries" window (search the registry, install a chosen version, update, uninstall), libraries saved in the project (`libraries` in `project.json`) and installed by Check on any computer, missing headers explained with suggested libraries, `s2a-cli lib`. To check with a real C++ project on a board.
3. **Wiring check in Live mode**: done on 2026-11-12 (chosen by the user), for 0.12.0. When Live mode starts, the inputs of the blocks are read for about a second (core `live::checkWiring`): a button always pressed, a floating digital or analog input, a distance sensor without echo or a DHT that does not answer give a "Check the wiring" card with what to check and the blocks concerned (hints only, the program runs). To check on real boards.

4. **Wiring from code** (asked by the user on 2026-11-12): the wiring diagram of a program written as text (expert mode, "My code"): core `wiring::componentsInCode` finds the components from the calls of the code (library objects, `pinMode` / `digitalRead` / `digitalWrite` / `analogRead` / `tone` / `pulseIn`..., simple pin constants; Python and JavaScript converted to C++ first) and gives blocks standing for them to `wiring::plan`; the table tells the line of each. For 0.12.0.

5. **Simulation without a board** (chosen by the user on 2026-11-12), for 0.13.0: a "Virtual board (no hardware)" port, chosen by itself when no board is plugged in. Live mode then runs on core `sim::VirtualBoard`, which answers the commands like the *S2A live* firmware (blocks, step-by-step mode, Python and JavaScript unchanged). The "Virtual board" panel draws the components of the wiring plan (`wiring::Connection::role` tells which board pin is which): LEDs, RGB LED, buzzer with sound, servo, DC motor, LCD / OLED screens, NeoPixel strip, LED matrix, the board's output pins; buttons, potentiometers, light, distance and DHT sensors and the phone are changed with the mouse. Upload and serial monitor refuse the virtual board with a clear message.

Phase 11 (packaging and release) stays dropped for now and may come back later.

Branches: `main` holds released states, `dev` integrates the work, each feature lives on its own
`feature/...` (or `fix/...`, `docs/...`) branch merged into `dev` through a pull request once CI is green.

## Architecture decisions

- **One application: `apps/webview`** - native C++ host embedding a system webview (library `webview/webview`, CEF as optional fallback). The UI (`frontend/`) is built from `scratch-blocks` + the `tools-react-components` theme and bundled **offline** inside the app (no server, no browser).
  - Decision of 2026-11-05: the Qt 6 native variant (B, `apps/native`, custom block canvas), built up to v0.3.0, is dropped to keep a single editor with the style of `tools-react-components`. Phases 3b and 12 are closed accordingly.
- **`core/` - `s2a_core` (C++17 static library)**, holding all the logic (tested without a GUI):
  - project model and `.s2a` format (zip: `project.json` + assets + format version) -> export / re-import;
  - data-driven block catalog (`blocks/*.json`, fr/en labels) - converted to Blockly JSON for the editor;
  - validator run **only** on Build/Run, returning kid-friendly localized errors tied to block IDs (the editor never raises errors);
  - code generator (block tree -> Arduino C++ `.ino`);
  - toolchain wrapper around a bundled `arduino-cli` (board detection, compile, upload, library install);
  - board database (Uno, Nano, Mega, then ESP32 / ESP8266);
  - transports: USB serial, Bluetooth (HC-05/HC-06, transparent serial), Wi-Fi (ESP, TCP/WebSocket);
  - live protocol talking to the *S2A live* firmware (`firmware/`, Firmata-like) for real-time control and debugging;
  - "My blocks" user library (saved block groups, no new block types);
  - i18n: French (default) and English.
- **Text languages**: read-only view of generated C++ first, then a C++ mode (sketch typed by the user, same toolchain as the blocks), Python (embedded `pocketpy`) and JavaScript (embedded `QuickJS-ng`) driving the board through the live protocol. A project stores its language and code (format version 2, written only for text projects so that block projects stay readable by older versions).
- **Install**: Windows `Scratch2Arduino_installer.exe` (Inno Setup or NSIS, `installer/windows/`) that installs the missing prerequisites and runs CMake configure/build/install, updating an existing installation incrementally (only changed sources are rebuilt); Linux/macOS `Scratch2Arduino_installer.sh` + top-level `Makefile` (`make`, `make install`, `make uninstall`) wrapping CMake.
- Project overview and layout: [README](../README.md).

## Roadmap

Each phase lists its **goal**, **deliverables** and **done when** criteria.

### Phase 0 - Setup and tooling
- **Goal**: an empty but buildable project on all three OS.
- **Deliverables**: `git init` and the planned layout; top-level `CMakeLists.txt` with option `S2A_BUILD_WEBVIEW`, `CMakePresets.json`; first `Scratch2Arduino_installer.sh`, `Makefile` and `installer/windows` script; CI (GitHub Actions) on Windows, Linux, macOS; `.clang-format`, `.clang-tidy`, `.editorconfig`; license; CONTRIBUTING draft.
- **Done when**: CI builds a "hello window" for each variant on the three OS, and `./Scratch2Arduino_installer.sh` / `make install` / `Scratch2Arduino_installer.exe` each install it.

### Phase 1 - Core foundations
- **Goal**: a shared, tested data model.
- **Deliverables**: project model and `.s2a` read/write with format versioning; JSON schema for the block catalog; board database (Uno, Nano, Mega: pins, PWM, analog, interrupts); i18n loader (fr default, en); GoogleTest suite.
- **Done when**: a project can be saved, reloaded and compared byte-for-byte in tests; strings switch between fr and en.

### Phase 2 - Arduino toolchain
- **Goal**: compile and upload without the Arduino IDE.
- **Deliverables**: bundled `arduino-cli` (downloaded at build time per OS); port and board detection; compile + upload of a hand-written sketch; parser turning compiler/upload errors into readable messages.
- **Done when**: Blink is uploaded to Uno, Nano and Mega from a core CLI test tool on all three OS.

### Phase 3a - Variant A MVP (webview)
- **Goal**: first usable app with scratch-blocks.
- **Deliverables**: webview host; `frontend/` (pnpm + Vite/webpack) embedding `scratch-blocks` with the `tools-react-components` theme; JS ↔ C++ bridge (project, build, serial); basic blocks (events, digital/analog write & read, wait, repeat/forever, if/else, variables, math, logic); save / open; board and port picker; Build and Upload buttons; code generator for these blocks.
- **Done when**: a beginner builds Blink with blocks and uploads it; the project reopens identically.

### Phase 3b - Variant B MVP (Qt native) - dropped
- **Dropped on 2026-11-05** (done in v0.3.0, then removed: only the webview application is kept).
- **Goal**: same MVP in pure C++/Qt.
- **Deliverables**: Qt window; block canvas (render, drag-and-drop, snapping, palette, zoom/scroll); same block catalog and code generator from `core/`; save / open the same `.s2a` files; Build and Upload.
- **Done when**: a project created in A opens, edits and uploads in B, and the other way round.

### Phase 4 - Validation and friendly errors
- **Goal**: errors only at Build/Run, clear and helpful.
- **Deliverables**: validator rules (missing inputs, unused/empty scripts, invalid pin for board, type mismatches...); fr/en child-friendly messages with "how to fix" hints; highlighting and focusing of the faulty block in both variants; compiler errors mapped back to blocks where possible.
- **Done when**: every validator rule has a test, and no error or popup appears while editing.

### Phase 5 - Live mode and real-time debugging
- **Goal**: instant feedback with the board connected.
- **Deliverables**: *S2A live* firmware (`firmware/`) and its protocol; one-click "Live" mode flashing it if needed; host-side interpreter running blocks against the board; running-block highlighting; live sensor/variable panel and graph; serial monitor.
- **Done when**: changing a block while in Live mode changes the board behaviour without re-uploading.

### Phase 6 - Sensors and actuators
- **Goal**: cover the usual beginner kits.
- **Deliverables**: block sets with wiring help images for LEDs, RGB LED, buttons, potentiometer, light sensor, buzzer, servo, DC motor (driver), ultrasonic HC-SR04, DHT11/22, LCD 16x2 (I2C module or 16 pins), OLED SSD1306, NeoPixel, LED matrix; automatic Arduino library installation via `arduino-cli`; support in both upload and live modes.
- **Done when**: each component has at least one example project tested on real hardware.

### Phase 7 - Productivity and sharing
- **Goal**: reuse and learn from others.
- **Deliverables**: "My blocks" library (save a selection, reuse, export/import); example project gallery; project export / import (`.s2a`) and templates; autosave and recent projects.
- **Done when**: a saved block group can be reused in another project and survives export/import.

### Phase 8 - Wireless
- **Goal**: control boards without a cable.
- **Deliverables**: Bluetooth transport (HC-05/HC-06); ESP32 / ESP8266 boards in the database; Wi-Fi transport for live mode; pairing / connection UI.
- **Done when**: live mode works over Bluetooth and Wi-Fi with the same blocks as over USB.

### Phase 9 - Text programming languages
- **Requested again on 2026-11-10** by the user, with a C/C++ mode for those who already know it (developed on its own branch for now).
- **Goal**: a path from blocks to text code.
- **Deliverables**: read-only Arduino C++ view synced with blocks; C++ mode (a sketch typed by the user, compiled and uploaded with `arduino-cli`, starting from the code of the blocks); Python mode (embedded `pocketpy`) and JavaScript mode (embedded `QuickJS-ng`) with an `arduino` API over the live protocol; editor with syntax highlighting, errors shown on their line after Check/Upload/Live; language and code stored in the project (format version 2).
- **Done when**: the same Blink program can be written in blocks, Python and JavaScript.

### Phase 10 - Documentation and education
- **Goal**: self-service learning for students, teachers and workshops.
- **Deliverables** (fr and en): user guide; step-by-step tutorials following the learner progression below; teacher / workshop kits (lesson plans, hardware lists); contributor documentation.
- **Done when**: a newcomer completes the first tutorial without outside help.

### Phase 11 - Packaging and release
- **Goal**: one-click installation everywhere.
- **Deliverables**: final Windows `Scratch2Arduino_installer.exe` (prerequisite check/installation, CMake build & install, shortcuts, uninstaller, signed); final `Scratch2Arduino_installer.sh` (dependency install via apt / dnf / pacman / brew) and `Makefile` (`make`, `make install`, `make uninstall`); macOS bundle; update check; release process and changelog; CONTRIBUTING, issue templates.
- **Done when**: CI produces release artifacts for the three OS, installed and smoke-tested on clean machines.

### Phase 12 - Variant comparison - dropped
- **Dropped on 2026-11-05**: decision taken early, only the webview application is kept.
- **Goal**: decide the long-term direction.
- **Deliverables**: comparison of A and B once both finish Phase 6 - tests with children, performance, binary size, maintainability, contributor friendliness.
- **Done when**: a written decision (keep both, or focus on one).

### Phase 13 - Automatic wiring diagram
- **Requested on 2026-11-09** by the user: the diagram of any project, not only of the examples.
- **Goal**: show the wiring a project needs, computed from its blocks.
- **Deliverables**: component and pin list extracted from the blocks by the core (the code generator already knows which component uses which pins); a description of each component (pins, parts that blocks do not tell - LED resistor, button to GND with pull-up, potentiometer on V0 of a 16-pin LCD, power) shared with `tools/wiring/wiring.py`; automatic placement on a breadboard and wire routing for Uno, Nano and Mega; a "Wiring" view in the editor that updates with the blocks, with a component / pin table for when the drawing gets crowded; warnings for pins used twice, I2C pins (SDA/SCL) taken by other blocks, too many components for the 5V supply; export as an image.
- **Done when**: the diagram of every example project is generated from its blocks (no hand-written description left in `tools/wiring/wiring.py`) and a project with a component of each kind gives a readable diagram.

## Learner progression (example projects)

1. **Blink** - digital output, wait, forever.
2. **Traffic light** - sequences, several LEDs.
3. **Button and LED** - digital input, if/else.
4. **Potentiometer and servo** - analog input, mapping values.
5. **Distance alarm** - ultrasonic sensor, buzzer, thresholds.
6. **Thermometer display** - DHT sensor, LCD/OLED, variables.
7. **Reaction game** - LED matrix, buzzer, buttons, score, random.
8. **Remote robot** - DC motors controlled over Bluetooth or Wi-Fi.

## Resources

- [scratch-blocks](https://github.com/scratchfoundation/scratch-blocks) (Blockly 12, TypeScript) - block editor.
- `@epitech/tools-react-components` (React + Mantine, fr/en locales) - design system.
- [arduino-cli](https://github.com/arduino/arduino-cli) - compile, upload, board and library management.
- [Firmata](https://github.com/firmata/protocol) - inspiration for the live protocol.
- [webview/webview](https://github.com/webview/webview) - native webview for the application; [CEF](https://bitbucket.org/chromiumembedded/cef) as fallback.
- [pocketpy](https://github.com/pocketpy/pocketpy), [QuickJS-ng](https://github.com/quickjs-ng/quickjs) - embedded Python and JavaScript.
- [GoogleTest](https://github.com/google/googletest) - C++ unit tests.
- [Inno Setup](https://jrsoftware.org/isinfo.php) / [NSIS](https://nsis.sourceforge.io/) - Windows installer.
