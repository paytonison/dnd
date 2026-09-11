# Local macOS packaging

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

Application package metadata is **v1.0.0**. New bundles derive both macOS version fields, `package-inventory.json`'s `applicationVersion`, and the packaged notice heading from the CMake application version. `Notices/APPLICATION-VERSION-POLICY.md` carries [the release policy](versioning.md). Dated archive hashes below remain historical and are not relabeled by this documentation update.

## Installed command-line tool

The Qt-free CLI can be installed separately from the desktop bundle using the configured build:

```sh
cmake --build build --target dnd-cli
cmake --install build --component cli --prefix /chosen/install/prefix
/chosen/install/prefix/bin/dnd-cli evaluate character.dnd.json
```

For a multi-configuration build, supply the same `--config` to the build and installation commands. The installation contains `bin/dnd-cli` (`dnd-cli.exe` on Windows), content under `share/dungeoning-a-dragon/packs`, and application/content license metadata. Move the entire prefix together. The CLI locates this data relative to its actual executable, including when invoked through `PATH` from another directory. It remains independent of Qt.

An explicit final pack-directory argument to `evaluate`, `sheet`, `preview`, or `apply` always takes precedence. Only the original executable in its configured development build can fall back to checkout data. A relocated installation with absent packs or a missing saved pack version reports the missing exact version; it does not substitute checkout data or a newer installed version.

The registered `installed-cli` CTest installs the CLI component, relocates the prefix, and invokes the actual executable through `PATH`. It verifies evaluation and HTML export of the preserved version-1 Wizard fixture, explicit-directory precedence, action failure without output writes, exact-version rejection, and saved-file preservation. This is separate from the macOS app ZIP workflow below.

## Desktop app archive

Run `./scripts/package-macos.sh` from any directory on a Mac with CMake, a C++20 toolchain, Qt 6 (including `qmake` and `macdeployqt`), Python 3.9+, and nlohmann/json. The first run also needs network access to cache the license texts from the exact Qt release's source repository. The application itself remains offline and account-free.

The script builds current sources in Release mode, copies the character content packs, runs `macdeployqt`, and inspects every deployed Mach-O file. It rejects unresolved library references, non-system absolute dependency paths, and mismatched architectures; removes redundant build-machine search paths; and checks the resulting code signature. It then extracts the actual ZIP into a temporary directory outside the checkout and runs the strict character/save/reopen/PDF smoke workflow with Qt and dynamic-linker overrides unset. Both Cocoa and the matching offscreen platform plugin are bundled.

When changing artwork, run `./scripts/build-icons.sh` before building or packaging to regenerate the checked-in ICNS and ICO containers from `assets/icons/app-icon.png`. The app embeds that PNG for Qt, and the macOS bundle uses `app-icon.icns`. The [branding guide](branding.md) records the current Golden Gate style artwork and its separate asset, native, and packaging checks. The static icon containers do not provide Icon Composer's adaptive appearances.

Successful output appears in:

- `dist/Dungeoning a Dragon.app`: self-contained local app bundle.
- `dist/Dungeoning-a-Dragon-macOS.zip`: archive preserving the app bundle and framework symlinks.
- `dist/Dungeoning-a-Dragon-macOS.zip.sha256`: archive checksum.
- `dist/packaged-smoke/`: the character, PDF, and screenshot from the relocated bundle's acceptance run.

Builds use the current installed Qt libraries and current checkout, including uncommitted changes. This is a reproducible procedure for those inputs, not a promise of byte-identical archives across toolchains, dependency versions, or builds. `Contents/Resources/package-inventory.json` records the source commit, working-tree state, Qt version, exact deployed binary paths, architectures, load commands, and minimum macOS versions.

## Configuration

Environment overrides are optional:

| Variable | Default | Purpose |
| --- | --- | --- |
| `DND_PACKAGE_BUILD_DIR` | `build-release` in the checkout | Release build and license cache |
| `DND_PACKAGE_DIST_DIR` | `dist` in the checkout | Generated package directory |
| `DND_QMAKE` | `qmake` on PATH | Select the Qt installation |
| `DND_MACDEPLOYQT` | The selected Qt installation's tool | Matching deployment executable |
| `DND_PACKAGE_PYTHON` | `python3` on PATH | Inventory and signing helper |
| `DND_PACKAGE_ARCHITECTURES` | Host architecture | CMake architecture selection |
| `DND_PACKAGE_DEPLOYMENT_TARGET` | `14.0` | Application compile target; deployed dependencies can require newer macOS |
| `DND_PACKAGE_JOBS` | `4` | Parallel build jobs |

Universal output requires universal versions of **every** linked library. Setting two architectures cannot turn an ARM-only Qt installation into a universal runtime. The script records the architecture of each deployed binary and the highest minimum macOS version found, then sets `LSMinimumSystemVersion` to that actual runtime requirement. For example, this workstation's Qt 6.11.2 libraries target macOS 14, while its Homebrew ICU libraries target macOS 26; those dependencies require a macOS 26-or-newer package.

## Notices and local trust

`Contents/Resources/Notices` includes the application's BSD-3-Clause license, the exact Qt version's LGPL/GPL texts, installed Qt and dependency license notices, SPDX inventories where available, and Homebrew build receipts/recipes. The current dragon artwork's provenance, source CC0 terms, and adaptation record appear in `APP-ICON-ATTRIBUTION.md`; the source credit is also available in the application's About dialog. Each content pack retains its own license and publication references. Qt remains dynamically linked. Before public redistribution, review the exact selected dependency licenses and corresponding-source obligations for that release. This workflow produces a local validation artifact and performs no external upload.

The package uses a local **ad hoc signature**. It has no Developer ID identity, notarization ticket, or stapled Apple ticket. Local signature verification establishes bundle integrity; it does not establish Apple's distribution trust. A downloaded copy may therefore be stopped by Gatekeeper. Ordinary development and local testing do not require notarization. A public Developer ID release would require a separately authorized signing/notarization workflow and credentials.

Useful checks for an existing artifact:

```sh
codesign --verify --deep --strict --verbose=2 'dist/Dungeoning a Dragon.app'
codesign --display --verbose=4 'dist/Dungeoning a Dragon.app'
otool -L 'dist/Dungeoning a Dragon.app/Contents/MacOS/Dungeoning a Dragon'
plutil -p 'dist/Dungeoning a Dragon.app/Contents/Info.plist'
```

The automated package test uses Qt's offscreen platform, so it establishes relocated library/plugin loading, bundled content discovery, calculations, persistence, and PDF generation. Native window, file-panel, and print-dialog interaction are separate acceptance checks. Windows and Linux packaging are not performed by this script.

The September 8, 2026 validation used macOS 27 on Apple Silicon and Qt 6.11.2. The final icon-inclusive archive contained 30 ARM64 Mach-O files, five Qt frameworks, and six plugins. Every non-system dependency resolved within the bundle; all nested signatures and the extracted application's signature passed strict verification. The extracted archive passed the complete strict smoke workflow, including the embedded icon check. Its runtime minimum was macOS 26.0 because of the bundled Homebrew dependencies. Earlier macOS versions and Intel Macs were not tested.

References: [Qt's macOS deployment guide](https://doc.qt.io/qt-6/macos-deployment.html), [Qt licensing](https://doc.qt.io/qt-6/licensing.html), and [Qt third-party components](https://doc.qt.io/qt-6/licenses-used-in-qt.html).

## September 8 continuation artifact

The final continuation package is under `output/continuation-20260908/package`, built from base `15a44da` plus the source hashes recorded in `output/validation/continuation-20260908/final-source-inputs.json`. Its ZIP SHA-256 is `32cac257c66407dc3f9e0987ab43c61c734fad080bdf887e23d33e1ed45dde30`. Dependency/signature audits, extracted-ZIP smoke, nine-file content parity, final native book workflow and 17-page PDF acceptance are recorded in [the continuation checkpoint](continuation-status.md). It remains an ARM64, macOS 26.0-minimum, Qt 6.11.2, ad hoc signed local artifact. Hosted platform runs and notarization are not established by this result.
