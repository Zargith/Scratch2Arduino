# Contributing to Scratch2Arduino

Thanks for helping! The project is in its early phases (see [`docs/roadmap.md`](docs/roadmap.md)),
so this guide is a draft and will grow with the code.

The [developer guide](docs/developer-guide.md) explains the architecture and gives step-by-step
recipes (add a block, a component, a board, a translation, an example, a tutorial).

## Ways to help

- **Bug reports and feature ideas**: open an issue with the *Report a problem* or *Suggest an idea* form
  (`.github/ISSUE_TEMPLATE/`). In the editor, *Settings > Report a problem* (and the crash screen) opens a new issue
  holding the same report template as text (GitHub only reads issue forms from the default branch), already
  filled with the version, OS, board and, after a crash, the error (`frontend/src/report.ts`).
- **Translations**: the interface is French first, English second; other languages are welcome.
- **Example projects and tutorials**: especially for classrooms and workshops.
- **Code**: pick an item of the current roadmap phase and open a pull request.

## Development setup

```bash
cmake --preset debug           # the application (needs WebKitGTK 4.1 on Linux)
cmake --build --preset debug
ctest --preset debug

cmake --preset core-only       # core library and tests only, no GUI dependencies
cmake --build --preset core-only && ctest --preset core-only
```

`./Scratch2Arduino_installer.sh --help` and `make help` list the other options.

## Conventions

- C++17, formatted with the repository's `.clang-format`; `.clang-tidy` describes the static checks.
- Code, comments, commit messages and developer docs in English; user-facing strings in French **and** English.
- The editor must never raise errors while the user is building a project: validation happens on Build/Run only,
  with friendly messages.
- Put logic in `core/` (tested without a GUI), keep `apps/webview` and `frontend/` thin.
- Add or update unit tests (GoogleTest, `core/tests/`) with every core change.
- New blocks: see [docs/block-catalog.md](docs/block-catalog.md). New interface texts go in both
  `resources/i18n/fr.json` and `resources/i18n/en.json` (a test fails if a key is missing in one of them).

## Branches

- `main`: stable states only, updated from `dev`.
- `dev`: integration branch; open pull requests against it.
- One branch per change: `feature/...`, `fix/...`, `docs/...`, `ci/...`, merged into `dev`.
- `dev` is merged into `main` when its CI is green (typically at the end of a roadmap phase).

## Releases

Every push to `main` whose CI passes publishes the GitHub release `vX.Y.Z`, where `X.Y.Z` is the
`VERSION` of `CMakeLists.txt`, with `Scratch2Arduino_installer.exe` attached (0.x versions are marked as pre-releases).
If that tag already exists nothing is published: bump `VERSION` on `dev` before merging into `main`.
Version numbers follow semantic versioning from 0.10.0 on: a release holding only fixes bumps the patch
number (0.10.1, 0.10.2... as many as needed), a release with new features bumps the minor number (0.11.0).
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `docs:`...).

## License

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).
