# Continuation checkpoint — September 8, 2026

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

Dated results below retain their original tested snapshots; the current documentation version does not relabel or rerun those artifacts.

This follows `plan-followthrough.md`. The complete roadmap remains open. The working base is `15a44da7d06125935afb2f8e04aef88282f254a4`; validation below distinguishes successive working-tree snapshots rather than treating them as one unchanged artifact.

## Scope decision

The user explicitly requested that human B/X levels above 14 remain unavailable for now. This defers the original continuation requirement without redefining the published maximum. The source ledger now distinguishes the already printed saving-throw bands and Cleric 11+ turning row from the unresolved later progressions and the X8/X26 attack-example conflict.

## Completed foundation verification

The foundation snapshot passed all three registered CTest entries: **38,623 assertions in 164 core cases**, the relocated installed CLI test, and **32 QtTest entries** including setup and cleanup. Exact commands:

```sh
cmake --build /tmp/dnd-spec-review-20260908 --parallel 4
ctest --test-dir /tmp/dnd-spec-review-20260908 -V
```

`output/validation/continuation-20260908/foundation-source-inputs.json` records the revision, working-tree paths and source hashes. `foundation-build.txt` and `foundation-ctest.txt` contain this run. It includes species/lineage validation and sourced species identity on the expanded SRD sheet. It precedes the final composite-label renderer and physical-spellbook lifecycle changes.

Native Cocoa interaction created Mira of the Cairn, entered six accepted ability totals, equipped chain mail/sword/shield, advanced Fighter 1 to 2, and inspected the independent HP arithmetic: `(7 + 1) + (5 + 1) = 14`. A reasoned override to 17 was saved, reopened and exported through native file panels. Disabling the required exact B/X source produced inspection-only mode and disabled Save/Save As; reenabling it, saving and reopening preserved identical document JSON. Separate fixtures verified inherited reroll permission with an unrelated character option, an explicit false exception, accepted-roll preservation, and an explicit missing-HP roll retaining the other level's accepted result. The detailed scope and artifacts are in `native-workflow.json` in the same validation directory.

Every page of the initial B/X, Wizard and Sorcerer exports was inspected (2, 5 and 7 pages). That inspection found a composite feature label separated from its first value and an obsolete forced Feats break leaving a nearly empty Sorcerer page. Both have focused actual-Qt layout regressions and source fixes. The final focused renderer run passed 13 Qt entries: eight section variants across 40 boundaries each, two composite variants across 70 boundaries each, Feats flow, setup and cleanup. The initial PDFs are before-state evidence, not final visual acceptance. `pdf-pages/foundation-pdf-qa.json` records their hashes and findings.

## Completed changes after the foundation snapshot

- Unsupported alternate species identities and unexecuted Dragonborn/Goliath progression variations fail before availability; compatible species replacements retain their identity, sources and interpreted mechanics. Lineage controls have precise type and supported-value checks. The expanded SRD sheet now includes the selected species.
- Section and composite calculation labels stay with their first content. The obsolete forced Feats break is removed. The SRD module supplies transient sheet metadata that excludes internal ledgers from printed appendices while retaining readable inventory/book sections, user notes, currency, sources and overrides. Other nested resource values render structurally.
- Physical Wizard books use exact owned inventory instances, independent written contents and receipts. Explicit backup/reconstruction preserves accepted preparations, obeys original copying costs and gates book-dependent study. New research goes only to the selected book; a recovered original retains its own contents. Imported Wizard progression follows its selected casting tables. First-Wizard multiclass acquisition, history, origin-spell separation and legacy migration boundaries are covered.
- Native review caught appended values being shown as “Not set” in action previews. Array append operations now display the actual resulting list, with a public-command GUI regression that checks spell names, payment and preview purity.
- The full suite exposed a regression in the feature layer's existing Context-based Wizard contract. The shared acquisition helper now accepts that resolved context explicitly. Existing assertions were preserved; both the focused feature/book union and the subsequent combined suite passed.

## Final verification

The final source uses base revision `15a44da7d06125935afb2f8e04aef88282f254a4` plus the recorded working-tree changes. `output/validation/continuation-20260908/final-source-inputs.json` includes the new source/test files as well as tracked inputs; documentation was finalized after runtime verification. The code hashes are the tested/package inputs. The user's pre-existing `.gitignore` change remains separate.

- **Combined automated suite:** all three CTest entries passed: **38,924 assertions in 176 core cases**, the installed CLI relocation test, and **37 QtTest entries** including setup and cleanup. Commands are the build/CTest commands above; final output is in `final-build.txt` and `final-ctest.txt`. The earlier failed Context run and superseded passing snapshots remain separate logs.
- **Focused lifecycle and UI evidence:** physical books passed **301 assertions in 12 cases**; the feature/book Context regression union passed **9,253 assertions in 33 cases**. Native-discovered preview and PDF issues have focused Qt regressions. These counts describe overlapping suites, not additional independent coverage totals.
- **Native Mac:** the B/X workflow described above was followed by the packaged Wizard workflow. Native Actions copied Alarm for 60 minutes/1,000 cp, applied/saved/reopened it, recorded loss of the original book, and recovered that exact original. The shared CLI reconstructed still-prepared Burning Hands into the backup for a further 60 minutes/1,000 cp; that intermediate state opened natively. The recovered state reopened and exported from the final package. Original six-spell contents, the independent two-spell backup, choices and accepted rolls were preserved; final currency is **8,500 cp**. Saved native copy and recovery states exactly match independently repeated CLI previews. See `native-workflow.json`, `native-spellbooks.json` and `reconstruction-cli.json`.
- **PDF:** all **17 final pages** passed visual acceptance: Mira 2, Wizard 20 5, Sorcerer 20 6, recovered physical-book Wizard 4. The last three baseline exports were rerendered after the final metadata change, and all 13 baseline pages were pixel-identical to the individually inspected renders. The four clean physical-book pages were inspected individually. No blocking clipping, overlap, orphaned label or missing visible provenance remained. A minor continuation limitation is recorded: one Wizard resource's remaining rows do not repeat its label, although the label stays with its first row. The exact PDF hashes and page evidence are in `final-pdf-pages/final-pdf-qa.json`. Only `wizard-physical-books-clean.pdf` is the final physical-book export; the earlier ledger-dump PDF is superseded evidence.
- **Installed CLI:** the final CLI component was installed and relocated, then evaluated and exported the legacy Wizard fixture while a per-process macOS sandbox denied reads of checkout data. A direct denied-read probe verified that restriction. Explicit directory precedence and missing-exact-version failures preserved the character bytes; linkage remained Qt-free. See `isolated-cli.json`.
- **Mac package:** `DND_PACKAGE_BUILD_DIR=/tmp/dnd-continuation-release-20260908 DND_PACKAGE_DIST_DIR=/Users/paytonison/Developer/dnd/output/continuation-20260908/package ./scripts/package-macos.sh` built the final package. Dependency and nested-signature checks, extracted-ZIP relocated smoke, and byte-for-byte parity for all nine bundled pack files passed. The 29,348,552-byte ZIP has SHA-256 `32cac257c66407dc3f9e0987ab43c61c734fad080bdf887e23d33e1ed45dde30`. This ARM64 local ad hoc signed package uses Qt 6.11.2 and requires macOS 26.0 due to its bundled dependencies; it is not a notarized public release. See `final-package.txt` and `final-package-acceptance.json`.

## Remaining scope

- Human B/X continuation above 14 remains unavailable at the user's request. The original maximum is not redefined.
- Older untracked saves that already lost their original spellbook preserve their data and preparations, but automatic reconstruction of missing physical history remains unsupported. They cannot activate tracking on a blank replacement to obtain free historical contents.
- Remaining SRD magic-item effects, broader equipment/container/encumbrance behavior, spell storage/absorption, scroll casting, crafting, companion combinations and source-specific persistent exceptions still require completion and acceptance. This spellbook milestone does not close the entire SRD lifecycle gate.
- Hosted job discovery returned no runs for `paytonison/dnd`; hosted macOS, Windows and Linux results remain unverified. Other architectures/older macOS and Windows/Linux native packaging were not tested.
- The later editions and supplemental publications listed in `PLAN.md` remain open.

No source-publication, rules-module, pack, or character save-format version changed. Generated builds, PDFs and acceptance artifacts remain outside version control.
