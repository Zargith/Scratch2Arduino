# Scratch2Arduino roadmap

Phased plan from the empty repository to a release. Each phase has a goal, deliverables and "done when" criteria.
**Every phase is now done or dropped** (version 0.14.0, 2026-11-13): this file keeps the plan as a history, and the
[remaining work](#remaining-work) lists what is still to check and what may come next.

## Status

| Phase | Status |
|---|---|
| 0 - Setup and tooling | Done |
| 1 - Core foundations | Done |
| 2 - Arduino toolchain | Done: compile checked in CI on the 3 OS, upload tested on an Arduino Uno and a Mega 2560 |
| 3a - Variant A MVP (webview) | Done: built and tested in CI on the 3 OS, used on real boards |
| 3b - Variant B MVP (Qt native) | Dropped (built in v0.3.0, removed in favour of the webview application) |
| 4 - Validation and friendly errors | Done: 10 rules with fr/en messages and hints (blocks not attached to a start block are greyed like comments, not reported), faulty blocks highlighted, compiler errors mapped to blocks |
| 5 - Live mode and real-time debugging | Done, tested on a Uno: firmware, protocol, interpreter, Live button (running scripts glow, variables and sensors with graphs), serial monitor and "write to the serial monitor" block, program output and errors in a Live console |
| 6 - Sensors and actuators | Done: every listed component has blocks in upload and Live modes, with automatic library installation, plus joystick, PIR sensor, infrared obstacle sensor, 28BYJ-48 stepper motor and TM1637 display; example projects with wiring diagrams. Part of the components is tested on real hardware (see the README) |
| 7 - Productivity and sharing | Done: "My blocks" library (export/import as `.s2ablocks`); example gallery with wiring diagrams on New; recent projects menu; autosave every 15 s with recovery at start-up; functions with up to 4 number inputs |
| 8 - Wireless | Done, to be checked on real hardware: ESP32 (USB upload, Live over Wi-Fi and Bluetooth), ESP8266 (Live over Wi-Fi), Live mode and Phone blocks through an HC-05 / HC-06 module on Uno, Nano and Mega; wireless examples |
| 9 - Text programming languages | Done (official feature since 2026-11-11): "My code" file beside the blocks (C++, Python or JavaScript) whose functions and variables become blocks, Blocks / Code views; expert mode for whole programs in C++ (compiled and uploaded), Python (pocketpy) and JavaScript (QuickJS-ng) in Live mode; CodeMirror 6 editor, friendly errors on their lines; conversion between the three languages (`s2a-cli convert`) |
| 10 - Documentation and education | Done: user guide, 8 step-by-step tutorials and teacher / workshop kit in French and English (`docs/fr`, `docs/en`), shown in the editor ("Tutorials and help" panel); novice / advanced variants of the progression examples; interactive tutorial "My doorbell" checking each step by itself; developer guide (`docs/developer-guide.md`). A workshop with newcomers is still to run (see [remaining work](#remaining-work)) |
| 11 - Packaging and release | Done (no code signing yet): every green push to `main` publishes a GitHub release with the Windows installer (`.exe`, incremental updates), `.deb` and `.rpm` packages (udev rule for the serial ports) and a macOS `.dmg`, each installed and checked by CI; release notes with install steps; check for new versions in the Settings, with "save before update"; issue forms and a *Report a problem* button |
| 12 - Variant comparison | Dropped (only the webview application is kept) |
| 13 - Automatic wiring diagram | Done: components and pins computed from the blocks and from code (`wiring::plan`, `wiring::componentsInCode`, `s2a-cli wiring`), warnings (pin used twice, I2C pins taken, USB power), SVG diagram drawn by the core for every board (Uno, Nano, Mega, ESP32, ESP8266) with PNG export. Examples keep their hand-drawn breadboard pictures, shown first (decision of 2026-11-10) |

## Beyond the phases

The first plan (Phases 0 to 12) did not foresee everything. These features were added along the way, at the
user's request or because using the application showed they were missing, in the order they were released.
Phase 13 (automatic wiring diagram, asked on 2026-11-09) was itself added to the plan during development.

| Version | Feature | Why / what it brings |
|---|---|---|
| 0.2.0 | **`s2a-cli`** command-line tool | Planned only as a test tool for Phase 2; it grew into a real tool to check a classroom computer before a workshop: `boards`, `ports`, `setup`, `compile`, `upload`, then `generate`, `live`, `wiring`, `convert`, `lib`, `bluetooth` |
| 0.5.0 | **Incremental updates** of the Windows installer | Installing a new version over an old one only rebuilds what changed |
| 0.5.0 | Blocks **read a pin as HIGH / LOW** and **input with pull-up** | A button wired to GND without a resistor, the simplest wiring for beginners |
| 0.5.0 | **Logo** of the application | |
| 0.6.4 | **Loose blocks greyed like comments** | Blocks not attached to a start block are no longer reported as errors: they wait, as in Scratch |
| 0.6.4 | **Functions** (define a group of blocks once, call it anywhere) | Not in the first block list; needed by the Traffic light and Remote robot examples |
| 0.6.4 | **Keyboard shortcuts** (save, copy, cut, paste, undo, redo) | |
| 0.6.4 | **Musical notes** (37 notes, do 3 to do 6) instead of raw frequencies | Easier for children than hertz |
| 0.6.4 | Installer **progress window**, check that the program is closed, **projects opened by drag and drop** on the icon and by double-click on a `.s2a` file | |
| 0.7.0 | **16-pin LCD without I2C module** and example variants (thermometer with an I2C or a 16-pin screen) | Many kits ship the bare screen |
| 0.8.0 | **ESP32 dimmer** example | A first project to try an ESP32 board |
| 0.8.0 | **Phone blocks** (messages to and from a Bluetooth terminal app) and the Phone lamp example | Phase 8 only planned the computer talking to the board |
| 0.9.0 | **Zoom** into the wiring diagrams; **breadboard-style** diagram like a breadboard simulator; examples open on their hand-drawn layout | Readable diagrams for crowded projects |
| 0.9.0 | **"My code"**: functions and variables written in C++, Python or JavaScript beside the blocks, which become blocks; Blocks / Code views with autocompletion | Phase 9 only planned whole programs as text; "My code" is a gentler step from blocks to text |
| 0.9.0 | **Novice and advanced levels** of the progression examples (blocks only, or with "My code"), shown as variants of one gallery card | Same project for mixed groups |
| 0.9.0 | **Tutorials and guide inside the editor** ("Tutorials and help" panel, opened at the first start) | Phase 10 planned documents only |
| 0.10.0 | **Settings window** (language, check for new versions) | |
| 0.10.0 | **Code conversion** between C++, Python and JavaScript (`s2a-cli convert`) | Changing language keeps the work done |
| 0.10.0 | **Dark theme**, and the program's output and errors in a **Live console** | |
| 0.11.0 | **HC-05 / HC-06 Bluetooth modules** on Uno, Nano and Mega: Live mode and Phone blocks through the module, Uno + HC-05 remote robot | Wireless without buying an ESP32 |
| 0.11.0 | **Arduino library manager**: search, install, update, uninstall; libraries saved in the project and installed by Check on any computer; missing headers explained (`s2a-cli lib`) | "Extensive library support" for "My code" and C++ projects |
| 0.12.0 | **Step-by-step Live mode**: Pause (F9) and Next block (F10), the next block lit, variables and pins followed | Real-time debugging |
| 0.12.0 | **Wiring check in Live mode**: inputs read for a second at start-up, "Check the wiring" card for a button always pressed, a floating input or a silent sensor (`live::checkWiring`) | Most beginner problems are wiring mistakes |
| 0.12.0 | **Wiring from code**: the wiring diagram of programs written as text (`wiring::componentsInCode`) | |
| 0.12.0 | **Save before update**: save, ignore or cancel before the update installer closes the program | No work lost on update |
| 0.13.0 | **Simulation without a board**: "Virtual board (no hardware)" port, Live mode on `sim::VirtualBoard` (blocks, step-by-step, Python and JavaScript), outputs drawn and inputs changed with the mouse | Try at home or before the parts arrive |
| 0.14.0 | **Interactive virtual board**: the project's wiring diagram comes alive in simulation (buttons pressed and sensors moved on the diagram, every pin used shown on the board); **foldable Live panels** | |
| 0.14.0 | **Functions with inputs** (up to 4 numbers), wide blocks on several lines | |
| 0.14.0 | **New components**: KY-023 joystick, HC-SR501 PIR sensor, FC-51 / KY-032 obstacle sensor, 28BYJ-48 stepper motor, TM1637 display, with their examples; push buttons drawn as **4-leg tactile switches** | Parts found in the usual kits |
| 0.14.0 | **Interactive tutorial** "My doorbell": a box guides each step (blocks, wiring, virtual board, a little code) and checks it by itself | Learning without a teacher at hand |
| 0.14.0 | **Linux and macOS packages** (`.deb`, `.rpm`, `.dmg`) checked by CI | Phase 11 planned an installer script only |
| 0.14.0 | **Issue forms** and a **Report a problem** button (filled with the version, system and board) | Easier feedback from users |

## Remaining work

Everything the phases plan is built. What is left:

### Checks on real hardware

Tested by the user on an Arduino Uno and an Arduino Mega 2560 (Windows): upload, Live mode over USB, LEDs, buttons,
passive buzzer, servo and potentiometer, HC-SR04, DHT11, 16-pin LCD, MAX7219 matrix, and the examples Blink, Traffic
light, Potentiometer and servo, Distance alarm, Thermometer (16-pin LCD), Reaction game. The README keeps the
up-to-date list ("Tested on real hardware"). Still to run on a real board:

- Nano; Linux and macOS with a real board;
- NeoPixel strip (Light gauge), I2C LCD, OLED, RGB LED, DC motors (L298N), light sensor, joystick, PIR,
  infrared obstacle sensor, 28BYJ-48 stepper motor, TM1637 display;
- ESP32: USB upload, Live over Wi-Fi (board network and home network) and over Bluetooth, macOS pairing;
- ESP8266 (NodeMCU): Live over Wi-Fi;
- HC-05 / HC-06 on Uno, Nano and Mega (pins saved from the Bluetooth window, Live mode and "Bluetooth module" block,
  the Uno + HC-05 remote robot);
- the examples added after the first hardware tests (Button and LED, Joystick and LEDs, Motion alarm, Obstacle
  detector, Parking barrier, Countdown, Remote robot, Phone lamp, Wireless station, ESP32 dimmer, advanced variants);
- functions with inputs, step-by-step Live mode and the Live wiring check on a board;
- Python and JavaScript in Live mode, C++ projects, and the library manager with a real C++ project on a board.

### Workshop

- Run a workshop (or a test with a newcomer) on the first tutorial and on the interactive tutorial.
- Fix the guide, tutorials and teacher kit from what the workshop shows.

### Known limits

- On AVR boards (Uno, Nano, Mega) the Live firmware reads lines of 48 characters at most: long "send to the phone" texts are cut in Live mode (uploaded programs are not affected).
- The Windows installer and the macOS application are not signed with a certificate: Windows SmartScreen and macOS Gatekeeper ask for a confirmation the first time.
- No package for Intel Macs, Arch Linux or Raspberry Pi: they build from the sources with `Scratch2Arduino_installer.sh`.
- The CI fallback for the apt mirror (`.github/actions/setup-apt`, switch to `archive.ubuntu.com` when the Azure mirror does not answer) has not run for real yet: check it the next time the Azure mirror fails.

### Ideas for later

Not planned yet, to choose from the users' feedback:

- signed installers (Windows certificate, Apple notarization);
- more boards (Arduino Leonardo, Raspberry Pi Pico...) and more components;
- more interface languages (contributions welcome);
- more interactive tutorials.

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
- **Install**: release packages made by CI (`.deb` and `.rpm` with CPack, macOS `.dmg`, `cmake/S2APackaging.cmake`); Windows `Scratch2Arduino_installer.exe` (`installer/windows/`) that installs the missing prerequisites and runs CMake configure/build/install, updating an existing installation incrementally (only changed sources are rebuilt); Linux/macOS `Scratch2Arduino_installer.sh` + top-level `Makefile` (`make`, `make install`, `make uninstall`) wrapping CMake.
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
- **Requested again on 2026-11-10** by the user, with a C/C++ mode for those who already know it.
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
