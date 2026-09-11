# 5E first-slice validation — September 11, 2026

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

Dated results below retain their original tested snapshots; the current documentation version does not relabel or rerun those artifacts.

Original 2014 **5E** is now a separate experimental edition, `srd51@1.0.0`, with `srd51-core@1.0.0`. The implemented slice is Human Fighter levels 1–3, Champion, Acolyte, six Fighting Styles, ordinary starting equipment, ability generation, explicit advancement, and class-resource/rest actions. The revised 2024 edition is labeled **5.5E** and retains its existing `srd55` module and pack identities. This checkpoint does not claim complete SRD 5.1, Player's Handbook, first-release, or other-edition coverage.

## Tested artifact

The base revision is **`f6686554fca278dd15ecea6f17495dbe1a6f3d59` plus uncommitted working-tree changes**. No commit, push or public release was made. The revision alone does not contain these additions.

The additions are the `srd51` evaluator, content validator/internal header, 98-record pack with license, source ledger, guide, acceptance fixture, and core tests. Existing edits register the edition in CMake/the module registry, label 5.5E consistently, route 5E dice through module requests, add Qt and installed-CLI regressions, update the bundled-pack inventory expectation, and document the new scope decision.

[The tested-input manifest](../output/validation/5e-20260911/tested-source-inputs.json) records the working-tree paths and SHA-256 values of **78 runtime, test, content, asset and build inputs**. They were checked unchanged after final verification. The final evaluator SHA-256 is `d0075c558ba705c5d48e8f717c723a0dec6e1ef718a976ee474cba9e2ec7a1fb`. Documentation completion followed the runtime checks; it does not change the tested binaries.

Environment: macOS 27.0, ARM64, Apple LLVM 21.0.0, C++20, Qt/QtTest 6.11.2, repository CMake configuration in `build`. The rules and command-line executable remain Qt-free; `otool -L build/dnd-cli` listed only `libc++` and `libSystem`.

## Automated checks

Commands executed from the repository:

```sh
cmake --build build --parallel 4
./build/dnd_tests '[srd51]'
DND_SRD51_QA_DIR=/Users/paytonison/Developer/dnd/output/validation/5e-20260911 \
  ctest --test-dir build --output-on-failure
./build/dnd-cli editions
./build/dnd-cli validate-pack data/packs/srd51-core
./build/dnd-cli evaluate tests/fixtures/srd51-human-fighter1.json
otool -L build/dnd-cli
git diff --check
```

The final full suite passed **all three registered CTest tests** in **198.64 seconds**:

| Evidence | Result |
| --- | --- |
| `rules-and-content` | **39,528 assertions, 194 cases passed**, including preserved B/X and both existing 5.5E versions |
| Focused `[srd51]` run | **598 assertions, 18 cases passed** |
| `desktop-workflow` | **39 QtTest entries passed**, including setup/cleanup; zero failures/skips |
| `installed-cli` | Passed the existing relocation/legacy tests and new original-5E fixture, exact-pack-removal, non-substitution and input-preservation checks |
| Real pack validator | Valid: **98 content records** |
| Static saved Fighter fixture | Complete evaluation, exit **0**, independently expected **12 HP / AC 19** |
| Build/whitespace | Build succeeded without compiler warnings; `git diff --check` passed |

The focused regressions derive expectations from [the source ledger](sources-srd51.md). They cover Human ability increases; HP 12/20/28; style, proficiency and armor arithmetic; negative Constitution floors; published ammunition bundle pricing; per-key point-buy inheritance; alternate-ID profiles through validation/installation/resolution/evaluation; compatible and incompatible replacements; malformed nested/conditional inputs; exact versions and missing sources; preserved accepted dice; action rollback; original Second Wind/rest recovery; and consistent save/reopen/history/override behavior.

The new Qt cases create a character through individual editor paths, avoiding whole-fixture assignment that would conceal numeric JSON-pointer array creation. They exercise action preview/apply, fixed and rolled advancement, locked accepted inputs, resource recovery, explanations, normal/effective overrides, missing-source protection, explicit dice persistence, and inherited point-buy display.

Final logs: [build](../output/validation/5e-20260911/build-final.txt), [focused tests](../output/validation/5e-20260911/5e-focused.txt), [CTest summary](../output/validation/5e-20260911/ctest-final.txt), and [detailed CTest output](../output/validation/5e-20260911/ctest-final-detail.txt). Earlier files in the validation folder describe earlier snapshots; these final files supersede them.

## Native macOS workflow

The Cocoa application was operated through its actual menus, keyboard controls, action dialogs and native file panels. A separate copy of the level-1 **Rowan** fixture was saved under the validation folder; the tracked fixture remained unchanged.

1. Entered 300 XP, previewed and applied fixed Fighter level 2: maximum HP **12 → 20**.
2. Entered 900 XP, selected Champion, previewed and applied fixed level 3: maximum HP **20 → 28**, weapon critical threshold **19**.
3. Changed current HP to **10** in the current-resource dialog, then previewed and applied Second Wind with accepted die **4**: **10 + 4 + 3 = 17 HP**, remaining Second Wind **1 → 0**.
4. Saved and reopened through native panels. The resource dialog retained **17/28 HP**, **0/1 Second Wind**, **3/3 Hit Dice**, and **1/1 Action Surge**. The five history entries retain the two accepted gains and commands; no reroll occurred.
5. Restarted the final build after adding the explicit archetype sheet row, reopened the same file, observed **Martial archetype: Champion** and **Ready to play**, and exported through the native Save panel. The final attribution-only wording trim was then re-exported through the same Cocoa PDF path using `--export-pdf`, avoiding a footer-only third page.

The final native character remains open in the development app. [Native acceptance details](../output/validation/5e-20260911/native-acceptance.json), [saved character](../output/validation/5e-20260911/native-5E-Fighter.dnd.json), and [independent CLI evaluation](../output/validation/5e-20260911/native-evaluation.json) preserve the results. Native creation from every blank field, rest branches, override editing, and source failures were covered by Qt automation rather than repeated manually in this native session; those evidence classes are separate.

## PDF acceptance

Poppler rendered the final native, automated and packaged sheets. Every page of the representative output was inspected for clipping, overlap, pagination, edition identity, sources, resources and override annotations. **Twelve pages across six two-page 5E PDFs passed.** Unchanged final Fighter-1 renders were compared byte-for-byte with the inspected renders; direct packaged Champion pages likewise match the separately inspected relocated render.

- `5E-Human-Champion.pdf`: Mara Ashford, mixed fixed/accepted advancement, **26 maximum HP**, named Champion, **normal AC 19 / overridden AC 20**, and the reason and original arithmetic.
- `native-5E-Champion-final.pdf`: Rowan's native export, named Champion, **28 maximum / 17 current HP**, spent Second Wind and saved inventory.
- `package/5E-Fighter-1.pdf` and `package/5E-Fighter-1-relocated.pdf`: original Human Fighter baseline.
- `package/5E-Champion-3.pdf` and `package/5E-Champion-3-relocated.pdf`: Rowan through both packaged executable locations.

[The PDF acceptance manifest](../output/validation/5e-20260911/pdf-acceptance.json) records exact PDF and rendered-page hashes. Source PDFs are external reference material and are not included in the application distribution.

## Packaging

The documented script was run with a separate release build and output directory:

```sh
DND_PACKAGE_BUILD_DIR=/tmp/dnd-5e-release-20260911 \
DND_PACKAGE_DIST_DIR=/Users/paytonison/Developer/dnd/output/validation/5e-20260911/package \
DND_QMAKE=/opt/homebrew/bin/qmake \
DND_PACKAGE_PYTHON=/Users/paytonison/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  ./scripts/package-macos.sh
```

The archive was refreshed after the final archetype-label correction and attribution wording trim. Because a metadata-only change does not trigger the existing `POST_BUILD` pack copy, the generated Debug and Release bundles were explicitly refreshed with `cmake -E copy_directory data/packs <bundle>/Contents/Resources/packs`; the packaging log records the actual destination and parity verification. The final run verified **30 bundled Mach-O files**, dependency paths, architectures, nested/outer signatures, **12/12 content-file parity**, and the existing relocated B/X smoke. Both packaged and independently extracted GUI executables exported complete 5E Fighter-1 and Champion-3 characters with exit **0**. All **62 production inputs** in the package manifest stayed unchanged during the build. The installed CLI's separate CTest verifies the relocated original-5E save with installed data and missing exact-version behavior.

The final ZIP is **29,519,120 bytes**, SHA-256 **`6a03e6255f11566524a643d6b5aee945e9918e55999b266b7a8d97b44033aee0`**. It targets **ARM64 / macOS 26.0 or later** because of its dependency closure, uses Qt 6.11.2, and is **locally ad hoc signed, not notarized**. [The packaging report](../output/validation/5e-20260911/package/package-acceptance.json) contains the archive location, exact inputs, dependency inventory and command logs.

## Remaining scope

This is a verified starting slice of 5E, not completion of the edition. Other races/classes/backgrounds, further Fighter levels, feats/variant Human, spellcasting, multiclassing, exotic languages, starting-gold alternatives, special Lance/Net mechanics, magic items, general inventory transactions/containers/encumbrance, and supplements remain open. Great Weapon Fighting and Protection supply sheet guidance; encounter adjudication is outside the builder.

Windows/Linux hosted jobs were not inspected or run in this work. The local package is not a notarized public release. Earlier B/X normal-limit and delivery gates, the broader 5.5E lifecycle, and the remaining edition roadmap stay open as documented in `PLAN.md` and `coverage.md`.
