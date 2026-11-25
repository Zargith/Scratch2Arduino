# Developer guide

How Scratch2Arduino is put together, and the recipes for the most common contributions: a block, a
component, a board, a translation, an example or a tutorial. Read [CONTRIBUTING](../CONTRIBUTING.md)
first for the setup, conventions and branches. Developer documentation is in English (project
convention); user documentation is in French and English.

## 1. The big picture

```
frontend/ (React + scratch-blocks)  ──bridge──▶  apps/webview (C++ host)  ──▶  core/ (s2a_core)
   editor, dialogs, palette            window.s2a_*       binds the calls          all the logic
                                                                                       │
                                     arduino-cli (compile, upload) ◀───────────────────┤
                                     firmware/s2a_live (Live mode) ◀── serial / TCP / Bluetooth
```

- **`frontend/`** is the block editor: React 19 + Mantine (theme copied from `tools-react-components`),
  `scratch-blocks` / Blockly 12. It is built with pnpm + Vite into one offline bundle the app loads from
  disk. It never does the real work: it converts the workspace to a project and calls the host.
- **`apps/webview/`** is the native application: a [webview](https://github.com/webview/webview) window
  that loads the bundle and binds the `s2a_*` functions (`src/bridge.cpp`, mirrored by
  `frontend/src/bridge.ts`). Long tasks run on worker threads and answer the promise when done.
- **`core/`** (`s2a_core`, C++17, no GUI) holds everything else and is unit-tested with GoogleTest.
- **`tools/s2a-cli/`** exposes the core on the command line (`generate`, `compile`, `upload`, `live`,
  `wiring`, `ports`, `setup`...): handy to test without the GUI.
- **`firmware/s2a_live/`** is the Arduino sketch used by Live mode.

### Core modules (`core/include/s2a`, `core/src`)

| Module | Role |
|---|---|
| `project` (+ `archive.cpp`) | Project model, `project.json`, `.s2a` zip read/write, format versioning |
| `blocks` | Loads `blocks/catalog/*.json` (the block catalog) |
| `boards` | Loads `resources/boards/*.json` (pins, capabilities, FQBN, drawing) |
| `validator` | Rules run **only** on Check / Upload / Live; returns localized problems tied to block ids |
| `codegen` | Block tree -> Arduino C++ sketch (`.ino`) |
| `build` | Check / Upload pipeline: validate -> generate -> compile -> upload |
| `arduino_cli` | Wrapper around the bundled `arduino-cli`; `requiredLibraries` installs libraries from the `#include`s |
| `board_packages` | Optional board packages (`resources/board-packages.json`): state, size to download (read from the board indexes), install at the pinned version, removal with the archives only they used |
| `diagnostics` | Turns compiler / uploader output into friendly messages, mapped back to blocks |
| `live` | `LiveLink` (protocol client), `Interpreter` (runs the blocks on the computer), `Session` |
| `serial`, `network` | Serial ports, TCP (Wi-Fi) and Bluetooth sockets |
| `wiring` (+ `wiring_svg.cpp`) | Components and pins used by the blocks, warnings, breadboard SVG |
| `userdata` | Recent projects, autosave, "My blocks" library |
| `i18n`, `resources` | `resources/i18n/{fr,en}.json` messages; locating the resource folder |

### What happens on...

- **Check**: the frontend converts the workspace (`frontend/src/project/convert.ts`) and calls
  `s2a_build` -> `build` runs the `validator`; on problems they come back with block ids (highlighted by
  the editor); otherwise `codegen` writes the sketch and `arduino_cli` compiles it; compiler errors go
  through `diagnostics`.
- **Upload**: the same, then `arduino-cli upload` to the chosen port (`s2a_upload`).
- **Live**: `s2a_live_start` validates, then `live::Session` connects to the port, checks the firmware
  greeting (`kHello`, uploads `firmware/s2a_live` when it is missing or older) and runs the
  `Interpreter`; each edit sends the new project (`s2a_live_update`); the frontend polls
  `s2a_live_state` for the running blocks, variables and sensor values.

## 2. Build and test

```bash
cmake --preset core-only && cmake --build --preset core-only && ctest --preset core-only   # core + s2a-cli
cmake --preset debug && cmake --build --preset debug                                      # the app (WebKitGTK 4.1 on Linux)

cd frontend && pnpm install && pnpm test && pnpm dev   # editor in a browser (host calls report they need the app)
```

To catch memory errors and undefined behaviour (a time computation that overflowed in the Live handshake
was only found this way), run the core tests under the sanitizers from time to time:

```bash
cmake -S . -B build/san -DS2A_BUILD_WEBVIEW=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/san && ctest --test-dir build/san
```

The core-only build downloads `arduino-cli` next to `s2a-cli`
(`build/core-only/tools/s2a-cli/`), so `s2a-cli compile <project.json> --board uno` compiles locally.
CI (`.github/workflows/ci.yml`) builds and tests the three OS, runs the core tests under ASan and UBSan,
validates the block catalogs against their JSON schema, runs the frontend tests, compiles every example and the firmware for every board, and
builds both installers.

## 3. Recipes

### Add a block

The catalog format and its rules are described in [docs/block-catalog.md](block-catalog.md). In short:

1. Add the block to `blocks/catalog/<category>.json` (French and English `message` and `tooltip`). If it
   uses a component, set its `component` (see below).
2. **Code generation**: handle its `type` in `core/src/codegen.cpp`; add a test in
   `core/tests/test_codegen.cpp`.
3. **Live mode**: handle it in the `Interpreter` (`core/src/live.cpp`). If the board needs a new command,
   add it to `firmware/s2a_live/s2a_live.ino` (document it in the header comment), to `LiveLink`, and
   change `kHello` (`core/include/s2a/live.hpp`) so that older firmware is uploaded again.
4. **Validation**: if it can be misused (missing input, pin without the capability...), add a rule in
   `core/src/validator.cpp` with French and English messages + hint in `resources/i18n/*.json`, and a
   test in `core/tests/test_validator.cpp`.
5. **Libraries**: when the generated code includes an Arduino library, add its header to
   `requiredLibraries` (`core/src/arduino_cli.cpp`).
6. Use it in `examples/projects/all-blocks` (compiled for every board in CI and round-tripped by the
   frontend tests).
7. Document it in the user guide's block table if it opens a new possibility.

The editor needs no change: the palette, the menus and the tooltips are built from the catalog.

### Add a component (for the wiring diagram)

1. Describe it in `resources/components.json`: `name`, the `current` it draws (mA, for the USB power
   warning), its `connections` - each leg (`pin` text, `to` = an argument name of the block such as
   `PIN` or `TRIG_PIN`, or `5V`, `VCC` (the board voltage), `GND`, `SDA`, `SCL` - an optional `via` part such as a resistor, and a
   `short` label for the drawing) - and an optional `note`.
2. Set `"component": "<id>"` on the catalog blocks that use it.
3. Check the drawing: `s2a-cli wiring <project.json> --svg out.svg`; extend `core/src/wiring_svg.cpp` if
   the part needs its own picture, and add tests in `core/tests/test_wiring.cpp`.

### Add a board

1. Create `resources/boards/<id>.json`: `name`, `fqbn` and `platform` (as `arduino-cli` knows them),
   `voltage`, flash and RAM sizes, `builtinLed`, `digitalPins`, `analogPins`, `pwmPins`,
   `interruptPins`, `i2c`, `spi`, `usb` vendor:product ids (port detection) and a `drawing` (size,
   colour, headers in the order printed on the board; `"name|-"` marks a pin that must not be used).
   For an HC-05 / HC-06 module (`bluetoothModule`): `serialPorts` lists the hardware serial ports on two pins
   (`{ "port": 1, "rx": "19", "tx": "18" }` on the Mega) and `softwareSerialRx` the pins where SoftwareSerial
   can receive (left out when every pin can, as on the Uno). GPIO boards name their pins with an object
   (`{ "GP0": 0, ... }`, Pico; `{ "D1": 5, ... }`, NodeMCU); an on-board LED on no header gets its own pin
   (`"LED"`, Pico). The sketches use these names (`D1` is defined by the NodeMCU core), except with
   `"sketchPinNames": false`, when the core does not know them: the pin numbers are written instead (Pico:
   `GP13` becomes `13`).
2. Give it a package in `resources/board-packages.json` (every board must belong to one, the unit tests
   check it): an existing one when the board uses the same `platform`, else a new entry with its `id`, its
   `name` (fr, en), the `platform`, the `version` installed (pinned, so that every computer gets the same
   tools), the `indexUrl` of a core that Arduino does not publish (it becomes
   `ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS`) and an `estimate` in bytes (`download` and `disk`, measured by
   installing it in an empty folder: `s2a-cli board install <id>` then `s2a-cli board list`). The Windows
   installer offers it as a task (`installer/windows/board-packages.cmake`), the settings and `s2a-cli
   board` list it. Only the Arduino AVR package is `shipped` (its archives come with the installers).
3. Make sure `firmware/s2a_live` compiles for it, and add it to the CI compile steps. A new USB vendor goes
   into the Linux rule `resources/linux/60-scratch2arduino.rules` (a test checks every `usb` vendor is
   there), plus a `usb` subsystem line when the upload goes through a USB device that is not a serial port
   (Pico BOOTSEL).
4. Add tests in `core/tests/test_boards.cpp` and, for a new family, an example project.

### Translate

- Interface texts: `frontend/src/i18n.ts` (editor) and `resources/i18n/{fr,en}.json` (core messages:
  validator, errors, Live). A test fails when a key is missing in one language.
- Blocks: `message`, `tooltip` and dropdown `label`s in `blocks/catalog/*.json`.
- Components: `resources/components.json`. Examples: `examples/projects/gallery.json`.
- Documentation: `docs/fr` and `docs/en` (same file names in both).

A new language needs a new key in each of these places plus the language switch in the editor; open an
issue first.

### Add an example project

1. Build it in the editor and save it, or write `examples/projects/<id>/project.json`.
2. Add its card to `examples/projects/gallery.json`, in the learner progression order (variants share
   one card).
3. Draw its breadboard picture in `tools/wiring/wiring.py` (one function, registered in `EXAMPLES`):
   `uvx --from pillow python tools/wiring/wiring.py <id>` writes `wiring.png`. Look at it.
4. Describe it in `examples/README.md` (parts and pins table).
5. `pnpm test` (frontend) checks that every folder has a card and that the editor opens it; CI compiles it.

### Write or update a tutorial

Tutorials live in `docs/fr/tutorials` and `docs/en/tutorials` with the same file names, one per step of
the learner progression, each matching a gallery example. Quote labels, block texts and messages exactly
as the application shows them (`frontend/src/i18n.ts`, the catalog, `resources/i18n`), link the example's
`wiring.png`, and keep the structure: goal / time / parts / circuit / steps / challenges / "if it does not
work". Update both languages in the same pull request.

## 4. Principles to keep

- **No errors while editing**: the editor never shows an error or a popup while the user builds; problems
  are reported on Check / Upload / Live only, with a friendly message, a hint and the block highlighted.
- **Logic in the core**, tested without a GUI; the frontend and the host stay thin.
- **Projects are forever**: never rename a released block `type` or argument `name`; a newer project
  format is refused with an "update" message rather than misread.
- **Offline first**: the app runs without a network once the board tools are installed.
- **French first, English second**, for every user-facing text.
- **Never replace a file without asking**: save dialogs go through `chooseSaveFile`
  (`apps/webview/src/bridge.cpp`), which asks again when it adds the extension to the typed name.
- **Nothing downloaded runs unchecked**: each release publishes `SHA256SUMS.txt` (release job of
  `ci.yml`), and the in-app update checks the installer against it (`s2a::integrity::verifyFile`)
  in a new temporary folder before Windows runs it as administrator.
- **Accessible to every child**: the page language follows the chosen one (`<html lang>`), a state is never
  told by colour only (the LEDs of the virtual board say "on" or "off" in their name and have an outline
  when off), text that keeps arriving is a live region (the serial monitor is a `role="log"`).
- **Tutorial locks hold**: locking a block clears Blockly's undo history (`frontend/src/locks.ts`), since
  undo replays edits whatever the locks.
- **Generated code computes in `double`**, as Scratch does: an `int` is 16 bits on AVR boards (Uno, Nano,
  Mega), so `analogRead(A0) * 100` would wrap to a negative number. Add, subtract and multiply cast their
  first operand to `double`, divide already did.
- **Files the user opens are untrusted**: read JSON with `s2a::detail::parseUntrustedJson`
  (`core/src/json_input.hpp`, refuses text nested too deep), and read each field with `find` plus a type
  check, never `json::value()`, which throws when a field has another type. What can be repaired is repaired
  rather than refused: a second variable or function with the same id is dropped (the first is kept), a
  thumbnail too large is left out of the saved file.
- **Every validator rule has a test**: rules loop up to `Rule::Count`, so a new rule without French and
  English messages fails `EveryRuleHasMessagesInBothLanguages`.
- **No call from the editor may end the program**: a synchronous bind goes through `Bridge::bindSync`
  (`apps/webview/src/bridge.cpp`), which turns any exception into a rejected promise with a friendly
  message (details on stderr). A stopped Live session is torn down off the UI thread, and whatever
  needs its port again (new Live session, upload, serial monitor) waits for that teardown.
