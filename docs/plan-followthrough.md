# Plan implementation checkpoint — September 8, 2026

This checkpoint preserves the foundation work against `PLAN.md`. The entire plan is not complete.

## Implemented changes

- SRD class and subclass behavior resolves through supported profiles while preserving imported identities and sources, including persistent action, advancement, inventory, and companion callers. Integration cases cover all twelve alternate class/subclass profiles at levels 3 and 20, imported Fighter and Warlock actions, and save/reopen.
- Shared folder and in-memory content validation checks manifests, entries, sources, executable bindings, progression payloads, replacement kinds and mechanical roles. Unsupported variations fail explicitly; failed imports preserve the usable installation. Supported variation limits are documented in `content-packs.md`.
- B/X malformed memorization is retained with an exact-slot diagnostic. Evaluation, campaign controls, and explicit HP rolling share per-key option inheritance, including explicit false overrides and preservation of accepted rolls.
- B/X human level 14 is labeled as the printed-table coverage boundary. Original Expert X7–X8 permits human advancement through 36; the unresolved continuation profile remains an open first-release requirement. See `sources-bx.md`.
- Composite SRD AC, skills, Speed, Initiative, abilities, and modifiers show substituted arithmetic and contributing class/item sources. Tests include Draconic Resilience, Expertise, Luckstone, Gauntlets, Ioun Stone, movement modes, armor penalties, compatible replacement shields, negative custom AC, and normal/effective override persistence.
- The Qt-free CLI discovers installed content relative to its executable, preserves explicit-directory precedence, and fails on missing exact versions without using checkout data from a relocated binary. A registered CTest exercises the real relocated installation.

## Verification completed

The latest combined run used base revision `7f566e3` plus the implementation working-tree changes in `/tmp/dnd-spec-review-20260908`:

```sh
cmake --build /tmp/dnd-spec-review-20260908 --parallel 4
ctest --test-dir /tmp/dnd-spec-review-20260908 -V
```

All three registered CTest entries passed: **38,383 assertions in 161 core cases**, installed CLI relocation, and **24 QtTest entries** including setup and cleanup. The log is `output/validation/plan-followthrough/ctest.txt`. This run predates the additional pagination regression described below and must not be described as a complete test run of this checkpoint's final test file.

A separate relocated CLI installation was exercised with its configured source directory physically unavailable. Legacy evaluation/export, explicit directory precedence, exact-version rejection, and input-file preservation passed. Its dependency inspection showed only system C++ and system libraries.

The fresh local macOS package passed dependency, signature, bundled-pack parity, and extracted-ZIP smoke checks. Its 30 ARM64 Mach-O files use Qt 6.11.2 and require macOS 26.0 because of bundled dependencies. The archive is under `output/plan-followthrough/package`; its SHA-256 is `5392d873263e47ca16a1ec69fd649ea4533896a66a93f99eaccf2aedbff61c55`. This is an ad hoc signed local artifact, not a notarized release. Packaging evidence and source input hashes are under `output/validation/plan-followthrough`.

## Work at the stopping point

1. **PDF pagination remains open.** Inspection of the newly packaged B/X sheet found an orphaned Advancement heading at the bottom of page 1, with its first table row on page 2. `DesktopWorkflow::sheetSectionHeadingStaysWithItsFirstRow` was added to reproduce this using Qt layout across multiple page boundaries. This new regression has not yet been built or run. The proposed renderer fix has not been applied. Continue in `src/persistence.cpp`, where `renderSheetHtml` currently emits section headings separately from their tables; test a table-header grouping with the actual Qt renderer. Preserve the regression and inspect every page after the fix.
2. **Native acceptance is partial.** The current app was opened and a B/X character's name, six accepted ability scores, Fighter class, first level, and XP were entered through native controls. The complete native equip/advance/explain/override/save/reopen/export workflow was interrupted before completion. Automated workflow and offscreen smoke results are separate evidence.
3. **Repeat the combined suite after the pagination change**, regenerate representative B/X/Wizard/Sorcerer PDFs, inspect every page, and rebuild the package. The existing Wizard/Sorcerer exports in `output/plan-followthrough` have not received complete final visual acceptance.
4. **Hosted platforms remain unverified.** The repository's hosted run listing returned no runs. The workflow definition is not execution evidence for macOS, Windows, or Linux.
5. **The human B/X continuation decision is unresolved.** No unsupported level-15–36 spells, saves, turning, or thief-ability progression was invented. The first-release normal-maximum gate remains open.
6. **Expanded SRD lifecycle and later editions remain open.** Spellbook loss/backup/reconstruction was researched against original SRD pages 78–79 and 104, but no implementation was added. Existing item/lifecycle coverage labels and all later edition/publication requirements remain in force.

No source-publication, edition-module, pack, or save-format version was changed. Generated packages and acceptance artifacts remain outside version control.
