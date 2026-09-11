# Logo and app icon

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

The September 11, 2026 branding uses the supplied seated dragon as a ruby-red glass emblem. The standalone logo has a transparent background; the app icon places the emblem on a pale frosted rounded tile. The requested macOS Golden Gate style is expressed through rounded glass forms, refraction, and crisp highlights, informed by [Apple's Icon Composer guidance](https://developer.apple.com/icon-composer/).

The application uses static PNG, ICNS, and ICO artwork through its existing Qt and platform integration. Adaptive Icon Composer `.icon` appearances are not included. The three-pane interface, rules, saves, and application version remain unchanged by this branding work.

## Source and provenance

The user supplied `dragon-312035_1280.png` from Downloads, a 1280 × 916 PNG with alpha. Its SHA-256 is `d6687860c64ce3d1dd2f195a88eca1470bcf46773ef8e1a73e3650e992ef86b3`. The source file remains unchanged.

The matching [Pixabay record](https://pixabay.com/vectors/dragon-red-symbol-fantasy-isolated-312035/) credits Clker-Free-Vector-Images and lists August 3, 2014 as its published date. Local download metadata points to the Needpix mirror. Source identification uses that metadata, the matching asset name and silhouette, and the published record; a byte comparison with Pixabay's CDN was unavailable. [Pixabay's terms, section 4](https://pixabay.com/service/terms/), classify content published before January 9, 2019 as CC0. The source record and terms were checked on September 11, 2026.

[The standalone artwork notice](../assets/icons/ATTRIBUTION.md) preserves that credit, the source-license basis, and the adaptation record in both the repository and packaged app. Application code retains BSD-3-Clause. Earlier Welsh Dragon attribution belongs to the replaced artwork and historical archives.

## Artwork and integration

| File | Purpose |
| --- | --- |
| `assets/branding/dragon-logo.png` | Standalone transparent ruby glass dragon logo; displayed in the README. |
| `assets/branding/generation-prompts.json` | Generation prompts and processing provenance. |
| `assets/icons/app-icon-master.png` | Master composition for the app icon. |
| `assets/icons/app-icon.png` | 1024 × 1024 runtime PNG embedded as `:/icons/app-icon.png`. |
| `assets/icons/app-icon.icns` | Multi-resolution macOS bundle icon. |
| `assets/icons/app-icon.ico` | Multi-resolution Windows executable icon. |
| `assets/icons/ATTRIBUTION.md` | Source, licensing, and adaptation notice copied into packaging. |

Built-in image generation supplied the glass artwork. When the generated output included a baked checkerboard, the user authorized local cleanup to produce actual transparency. The recorded prompts and processing provenance distinguish generation from that cleanup and the subsequent platform encoding.

`src/app/main.cpp` sets the Qt application icon from the embedded PNG. CMake sets the macOS bundle icon to `app-icon.icns`, compiles `assets/app-icon.rc.in` with the ICO on Windows, and installs the PNG in the Linux hicolor icon theme. The About dialog credits the new source. The standalone logo is an additional asset and does not add a banner or otherwise change the builder layout.

## Rebuild and check

After updating the runtime PNG, run these commands from the repository root:

```sh
./scripts/build-icons.sh
cmake --build build --parallel 4
ctest --test-dir build -R '^desktop-workflow$' --output-on-failure
QT_QPA_PLATFORM=offscreen \
  "build/Dungeoning a Dragon.app/Contents/MacOS/Dungeoning a Dragon" \
  --smoke output/branding-validation/smoke
```

The icon script encodes the runtime PNG; it does not generate artwork. Building and packaging consume the checked-in containers, so regenerating them is an explicit step. Inspect the decoded ICNS and ICO at small sizes and compare the bundled icon with the approved source. The smoke command checks that the Qt icon resource loads, then exercises character save/reopen and PDF generation; its success alone does not establish artwork identity or visual quality.

Quit and relaunch the application to inspect the rebuilt resource and About dialog. For a separately packaged artifact, use the [existing packaging workflow](packaging.md) and inspect the newly extracted bundle. Native macOS appearance, automated workflows, image/container checks, and package verification are separate evidence classes.

## Validation status

Verified September 11, 2026 against base `f6686554fca278dd15ecea6f17495dbe1a6f3d59` plus the current uncommitted branding, 5E and version-documentation work. The application remains **v1.0.0**. Earlier archives retain their own assets and evidence.

- The 1254-pixel logo and icon master, and 1024-pixel runtime icon, contain genuine alpha ranging from 0 through 255. Painted checkerboard pixels were removed from the exterior; enclosed white specular highlights were preserved. The source PNG in Downloads is byte-unchanged.
- `scripts/build-icons.sh` generated **10 ICNS representations** and **7 ICO representations**. Both containers were decoded and checked for dimensions and real transparency. Small representations, including 16, 32, 48, 64 and 128 pixels, were inspected on light and dark backgrounds.
- The Debug build passed. The registered `desktop-workflow` test passed **39 QtTest entries**, zero failures or skips, in about 31 seconds. No rules or persistence behavior changed, so the full core rules suite was not repeated for this artwork change.
- The native macOS About dialog visibly displayed the new glass icon, v1.0.0, and the new source credit. Dock automation timed out; the Dock itself was not independently observed, and no global icon cache was reset. Native Windows and Linux shell presentation remains unverified.
- A fresh Release package passed dependency and strict nested/outer signature checks for **30 Mach-O files**, and the actual extracted archive passed the existing B/X save/reopen/export smoke, including presence of the embedded Qt icon. This smoke is not a new visual character-sheet acceptance claim.
- The actual ZIP's ICNS and both artwork notices match the current repository, as do the twelve bundled content files and release-policy notice. All 65 recorded package build inputs remained unchanged. The final archive SHA-256 is `8ca73796da4bce7c99a04104112ca7c278e5aeaab62625d8a9e42abfa5776bc3`; it remains ARM64, Qt 6.11.2, macOS 26.0 minimum, locally ad hoc signed and not notarized.

[Branding evidence](../output/branding-validation/branding-validation.json), [asset/container checks](../output/branding-validation/asset-checks.json), [desktop test output](../output/branding-validation/desktop-workflow.txt), and [package verification](../output/branding-validation/package-verification.json) record the actual outputs. The final visual comparison is [logo-and-icon.png](../output/branding-validation/logo-and-icon.png). The local cleanup recipe and raw generated inputs are retained under `output/branding-validation`; the reusable finished assets and generation prompts are in `assets/branding` and `assets/icons`.

To reproduce this package without replacing earlier archives:

```sh
DND_PACKAGE_BUILD_DIR=/tmp/dnd-branding-release-20260911 \
DND_PACKAGE_DIST_DIR=/Users/paytonison/Developer/dnd/output/branding-validation/package \
DND_QMAKE=/opt/homebrew/bin/qmake \
DND_PACKAGE_PYTHON=/Users/paytonison/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  ./scripts/package-macos.sh
```
