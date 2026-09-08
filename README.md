# Dungeoning a Dragon

A local desktop D&D character builder with a C++20 rules engine and Qt 6 Widgets interface. Work is in progress against [the implementation plan](PLAN.md). The implementation includes original 1981 B/X, the preserved experimental SRD 5.2.1 fighter/wizard interface proof, and an expanded SRD module with all 12 classes through level 20 under continued validation; [the coverage ledger](docs/coverage.md) distinguishes implementation from future editions and verification.

## Build

CMake 3.24+, a C++20 compiler, and Qt 6.4+ Widgets, Print Support, and Test are required for the desktop build. nlohmann/json and Catch2 3 are used when installed; otherwise CMake fetches pinned releases on the first configure. Using the application needs no network or account.

On macOS with Homebrew:

```sh
brew install cmake ninja qtbase nlohmann-json catch2
cmake --fresh -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open "build/Dungeoning a Dragon.app"
```

On other platforms, install Qt 6 and point `CMAKE_PREFIX_PATH` at its installation. The same CMake targets and CTest suite apply. The GitHub Actions workflow configures all three platforms; see the validation report for checks actually executed.

To build just the core and CLI, use `-DDND_BUILD_GUI=OFF`. The public headers under `include/dnd` and the core rules have no Qt dependency.

## Use

Create a character in **File → New**, then follow the stages on the left. Each stage's choices appear in the center; the live sheet appears on the right. Click a statistic to inspect its calculation and source. Validation explains missing inputs and unavailable selections. Explicit dice commands record accepted results; refreshing and reopening never reroll.

Advanced mode exposes campaign options and DM overrides. Hiding Advanced mode preserves existing settings. Overrides require a reason and retain both the normal and effective result. Current HP and other session resources are separate from creation and advancement inputs.

Save characters as `.dnd.json` files. Exact edition-module and content-pack versions are stored with the choices. Recovery copies are separate `.autosave` files. Missing rules or unsupported versions open for inspection without silently rewriting or substituting the original.

The app uses a transparent red Welsh dragon icon adapted from Sodacan’s artwork under CC BY-SA 3.0; credit and the artwork license are separate from the application code.

Use the source dialogs to select content packs, browse searchable options and their publications, and import structured pack folders. [Pack documentation and a template](docs/content-packs.md) describe the interface. PDF export and printing use the same evaluated sheet, including edition, sources, validation, and override annotations.

## CLI

```sh
./build/dnd-cli editions
./build/dnd-cli validate-pack data/packs/bx-core
./build/dnd-cli new bx rook.dnd.json
./build/dnd-cli new srd55 modern.dnd.json 2.0.0
./build/dnd-cli migrate legacy.dnd.json 2.0.0 migrated-copy.dnd.json
./build/dnd-cli evaluate rook.dnd.json
./build/dnd-cli sheet rook.dnd.json rook.html
```

Evaluation returns exit status `2` for an incomplete or invalid draft, `0` for a complete character, and `1` for a command or file error. It is expected that a newly created blank draft is incomplete. The optional final argument to `evaluate` and `sheet` selects an alternate pack directory. Installed CLI commands discover packs relative to the executable under `../share/dungeoning-a-dragon/packs`; an explicit directory takes precedence. Only the original development executable falls back to its configured checkout. See [installed CLI validation](docs/packaging.md#installed-command-line-tool).

Application code retains the repository's BSD-3-Clause license. Content license and publisher metadata are separate. SRD 5.2.1 material is attributed under CC BY 4.0. B/X is independently encoded mechanical data with original explanatory text; book scans, artwork, and copied book prose are excluded from distributions.

## Local Mac package

Run `./scripts/package-macos.sh` to build a Release app, bundle Qt and its dependencies, verify all library paths and signatures, and test an extracted ZIP away from the checkout. See [packaging notes](docs/packaging.md) for the actual architecture and minimum macOS version of the generated package. This is a local ad hoc signed build.

The expanded SRD module uses version `2.0.0` and its matching pack. Version `1.0.0` remains compiled and available for exact legacy saves. Select the version in the New menu; upgrades are explicit copies with a comparison preview. See [current expanded-module progress and remaining lifecycle work](docs/v2-progress.md).
