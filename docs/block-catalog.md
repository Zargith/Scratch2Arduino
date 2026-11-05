# Block catalog

Every block shown by the editors is described in JSON, one file per toolbox category, in
[`blocks/catalog/`](../blocks/catalog). The files are compiled into the application, and checked:

- against the JSON schema [`blocks/schema/catalog.schema.json`](../blocks/schema/catalog.schema.json)
  (CI job *Data files*; editors such as VS Code use it for completion thanks to the `$schema` key);
- by `s2a::BlockCatalog` (unit test `BuiltinCatalog.IsValid`), which also checks what a schema cannot.

## Example

```json
{
  "type": "pins_digital_write",
  "shape": "statement",
  "message": { "fr": "mettre la broche %1 à l'état %2", "en": "set pin %1 to %2" },
  "tooltip": { "fr": "…", "en": "…" },
  "args": [
    { "name": "PIN", "kind": "pin", "capability": "digital", "default": "13" },
    { "name": "STATE", "kind": "dropdown", "default": "HIGH",
      "options": [
        { "value": "HIGH", "label": { "fr": "HAUT", "en": "HIGH" } },
        { "value": "LOW", "label": { "fr": "BAS", "en": "LOW" } }
      ] }
  ]
}
```

## Rules

| Item | Rule |
|---|---|
| `type` | `lower_snake_case`, unique, prefixed by the category id (`pins_…`). Stored in projects: never rename a released block. |
| `shape` | `hat` (starts a script), `statement`, `c` (wraps stacks, needs ≥ 1 `statement` argument), `reporter` (number/text), `boolean`. |
| `terminal` | `true` when nothing can be attached below (`control_forever`). Not allowed on reporters and booleans. |
| `message` | French **and** English. `%1`…`%n` refer to the arguments in declaration order; each appears exactly once per language, in any order (`"ajouter %2 à %1"` / `"change %1 by %2"`). |
| `tooltip` | French and English, written for beginners. |
| `args[].name` | `UPPER_CASE`, unique in the block. Stored in projects. |
| `args[].kind` | `number`, `text`, `boolean` (slots), `pin`, `dropdown`, `variable` (menus), `statement` (inside a `c` block). |
| `default` | Allowed on `number`, `text`, `pin`, `dropdown` (must be one of the option values). |
| `min` / `max` | `number` arguments only. |
| `capability` | Required on `pin` arguments: `digital`, `analog`, `pwm` or `interrupt`. The menu lists the pins of the selected board that have it. |

## Adding a block

1. Add it to the right `blocks/catalog/<category>.json` (or create a category file with a new `order`).
2. Run the tests: `cmake --preset core-only && cmake --build --preset core-only && ctest --preset core-only`.
3. Generate its Arduino code in `core/src/codegen.cpp` and run it in Live mode in `core/src/live.cpp`
   (`Interpreter`); when Live mode needs a new board command, add it to the firmware
   (`firmware/s2a_live`) and to `LiveLink`, and change `kHello` so that older firmware is uploaded again.
4. When the generated code includes an Arduino library, add its header to `requiredLibraries`
   (`core/src/arduino_cli.cpp`): it is then installed automatically before compiling.
5. Use it in `examples/projects/all-blocks` (compiled for every board in CI, and round-tripped by the
   editor tests).
