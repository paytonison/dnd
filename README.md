# Dungeoning a Dragon

<img src="assets/branding/dragon-logo.png" alt="Dungeoning a Dragon ruby glass dragon logo" width="200">

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](docs/versioning.md).

A local desktop D&D character builder with a C++20 rules engine and Qt 6 Widgets interface. Work is in progress against [the implementation plan](PLAN.md). The implementation includes original 1981 B/X, a separate experimental **5E (2014)** Human Fighter through level 3, the preserved **5.5E (2024)** SRD 5.2.1 Fighter/Wizard interface proof, and an expanded 5.5E module with all 12 classes through level 20 under continued validation. [The coverage ledger](docs/coverage.md) distinguishes implemented scope, future work, and verification.

## Release policy

**v1.0.0** is the basic, minimum app. Subsequent **vX.0.0** major releases represent full implementations of game systems; **v1.X.0** updates add content and new features; **v1.0.X** patches fix defects and make corrections. Module, content-pack, publication and save-format versions are independent. See [the complete version policy](docs/versioning.md).

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

Choose **5E (2014) / SRD 5.1** to create a Human Fighter with the Acolyte background, six Fighting Style options, and Champion at level 3. The first slice includes starting equipment and ammunition purchases, explicit level advancement, Second Wind, Action Surge, and short/long rests through **Character → Actions…**. [The 5E guide](docs/srd51.md) explains the workflow, supported source profile, and remaining scope. **5.5E** names the revised 2024 rules; the two editions keep distinct saves and content packs.

The logo and app icon use the supplied dragon as a ruby glass emblem, with a pale frosted rounded tile for the macOS Golden Gate style app icon. The source is credited to Clker-Free-Vector-Images on Pixabay under its pre-2019 CC0 terms. [The branding guide](docs/branding.md) records the source, generated artwork, static PNG/ICNS/ICO integration, and validation; [the artwork notice](assets/icons/ATTRIBUTION.md) keeps its provenance separate from the application code.

Use the source dialogs to select content packs, browse searchable options and their publications, and import structured pack folders. [Pack documentation and a template](docs/content-packs.md) describe the interface. PDF export and printing use the same evaluated sheet, including edition, sources, validation, and override annotations.

## CLI

```sh
./build/dnd-cli --version
./build/dnd-cli editions
./build/dnd-cli validate-pack data/packs/bx-core
./build/dnd-cli new bx rook.dnd.json
./build/dnd-cli new srd51 fighter-5e.dnd.json 1.0.0
./build/dnd-cli new srd55 modern.dnd.json 2.0.0
./build/dnd-cli migrate legacy.dnd.json 2.0.0 migrated-copy.dnd.json
./build/dnd-cli evaluate rook.dnd.json
./build/dnd-cli sheet rook.dnd.json rook.html
```

Evaluation returns exit status `2` for an incomplete or invalid draft, `0` for a complete character, and `1` for a command or file error. It is expected that a newly created blank draft is incomplete. The optional final argument to `evaluate` and `sheet` selects an alternate pack directory. Installed CLI commands discover packs relative to the executable under `../share/dungeoning-a-dragon/packs`; an explicit directory takes precedence. Only the original development executable falls back to its configured checkout. See [installed CLI validation](docs/packaging.md#installed-command-line-tool).

Application code retains the repository's BSD-3-Clause license. Content license and publisher metadata are separate. SRD 5.1 and SRD 5.2.1 material each retain their CC BY 4.0 attribution. Supplementary 2014-rule references and reuse boundaries are documented in [the 5E source ledger](docs/sources-srd51.md). B/X is independently encoded mechanical data with original explanatory text; book scans, artwork, and copied book prose are excluded from distributions.

## Local Mac package

Run `./scripts/package-macos.sh` to build a Release app, bundle Qt and its dependencies, verify all library paths and signatures, and test an extracted ZIP away from the checkout. See [packaging notes](docs/packaging.md) for the actual architecture and minimum macOS version of the generated package. This is a local ad hoc signed build.

The expanded Wizard supports explicit physical spellbook tracking, backup, loss, reconstruction and recovery through **Character → Actions…**. Each book keeps its own contents and copying costs; [the lifecycle guide](docs/srd55-lifecycle.md#physical-wizard-spellbooks) explains activation and legacy limits. [The latest continuation checkpoint](docs/continuation-status.md) records current automated, native, PDF and package evidence and the remaining roadmap.

The expanded 5.5E (`srd55`) module uses version `2.0.0` and its matching pack. Its version `1.0.0` remains compiled and available for exact legacy saves. Select the version in the New menu; upgrades are explicit copies with a comparison preview. See [current expanded-module progress and remaining lifecycle work](docs/v2-progress.md). The separate 5E (`srd51`) module starts at `1.0.0`; its new implementation and delivery evidence is recorded separately in [the September 11 checkpoint](docs/5e-validation-20260911.md).
