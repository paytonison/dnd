# Delivery status against the supplied plan

This is a requirement audit, not a claim that the whole plan is finished. The active goal includes the expansion sequence. The first-release architecture, B/X lifecycle, experimental second-edition proof, and initial Mac acceptance have been implemented. Full edition expansion remains work to execute.

| Plan requirement | Current evidence | Status |
| --- | --- | --- |
| C++20, CMake, Qt6 Widgets/Print Support, JSON, Catch2, Qt Test | `CMakeLists.txt`, public headers, installed Qt6.11.2; native and headless builds | Implemented and built on Mac |
| Qt-independent engine and compiled edition modules | Core/edition targets; headless executable links only system C++ and system runtime | Verified locally |
| Edition-neutral document, exact ruleset, deterministic evaluation | `engine.hpp`, core and edition tests; incomplete/invalid draft cases | Implemented and tested for B/X and experimental SRD |
| Explain values and unavailable choices | Calculation source/step data, dynamic choice reasons, native inspector and QtTest | Verified locally |
| B/X seven classes through printed core tables | 12 B/X test cases, every supported class level, independent original-source expectations | Implemented and tested |
| B/X generation, adjustments, eligibility, alignment, languages, money, gear, combat, saves, abilities, spells, XP, advancement | `bx.cpp`, 186-entry pack, original B13 worked example, source ledger | Implemented within stated original-rule interpretations |
| First/higher-level creation and separate current resources | HP inputs per level, level history, current-resource editor, roundtrip tests | Implemented and tested |
| Published optional rules and identifiable house rulings | Campaign preset/advanced fields, reasoned exceptions, normal/effective display | Implemented and tested |
| Three-pane native layout, creation/open/save/recent/presets/source browser | `mainwindow.cpp`, complete GUI workflows, native Cocoa inspection | Verified locally |
| Advanced mode preserves selections | QtTest visibility/roundtrip checks | Verified |
| Reasoned choice/stat overrides cannot create missing rules | Common override validation, source guards, choice-exception QtTest | Implemented and tested |
| Clean print/PDF including sources and override annotations | Same sheet renderer; basic fighter plus level14 cleric/magic-user exports visually inspected | Verified on Mac; physical printer not used |
| Structured packs, schema, template and executable validator | `content.cpp`, schema/doc, example homebrew pack, invalid pack and dependency preflight tests | Implemented and tested |
| Publisher/origin/license distinction and deterministic conflicts | Manifest data, dependency/cycle/duplicate/replacement tests | Implemented and tested |
| Exact save versions, recomputation, source removal retention | Core and GUI roundtrip tests; missing-source and future-version originals preserved byte-for-byte | Verified |
| Atomic save and recoverable autosaves | Same-directory atomic replacement, flushes, partial-temp fixture, GUI autosave/recovery tests | Implemented and tested; no power-cut hardware test |
| Original source verification, printed-page references, ambiguities | `sources-bx.md`, original preview/full-scan overlap, visual tables and source snapshots | Recorded |
| Experimental SRD5.2.1 fighter/wizard1–3 through same interface | 11 SRD tests, complete fighter/wizard GUI roundtrips, all9 species/all4 backgrounds | Implemented and tested |
| Full SRD lifecycle, OD&D, Holmes, AD&D1e, BECMI/RC, AD&D2e,3.0,3.5,4e,2014 5e, supplements | `coverage.md`, next full-SRD implementation handoff | **Not completed** |
| BSD application code, separately licensed content, no distributed book scans/prose | Repository license, pack manifests, package notices, icon CC BY-SA credit | Implemented |
| macOS/Windows/Linux automated build checks | `.github/workflows/build.yml` matrix | Added; hosted Windows/Linux jobs not executed in this local task |
| Mac packaging and actual runtime | Release bundle/dependency/signature audit plus relocated extracted-ZIP smoke; `docs/packaging.md` | Local package acceptance recorded there |
| Offline operation, no account, no automatic edition conversion | Local-only application paths and explicit edition-specific documents | Implemented |
| User-supplied dragon as app icon | PNG, multi-resolution ICNS/ICO, Qt resource, Mac plist, Windows RC, Linux desktop entry; source attribution | Added and built |

The count of tests or source entries does not by itself prove full publication coverage. The edition ledgers describe what the tests exercise and what remains outside the builder. The next work is the full SRD lifecycle, preserving all current B/X and experimental acceptance cases, followed by the other specified edition profiles and supplements.

First-release milestone verification: `ctest --test-dir build-dev -V` passed 10,189 assertions in 30 Catch2 cases plus all 9 QtTest entries (including setup/cleanup); exact output is in `output/validation/ctest.txt`. The self-contained icon-inclusive archive passed the extracted-bundle smoke and strict signature/dependency audit.

Expanded module 2.0.0 is now implemented and tested through all 240 single-class states, with focused multiclass and migration regressions. The latest combined expanded-module run passed 34,980 assertions in 76 core cases and all 14 QtTest entries. See `v2-progress.md` for remaining full-SRD lifecycle work; the wider edition/supplement expansion remains incomplete.
