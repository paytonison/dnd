# AGENTS.md — Dungeoning a Dragon

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](docs/versioning.md).

## Application version policy

The application is **v1.0.0**, its basic, minimum version. Subsequent major releases (**vX.0.0**) require full implementations of game systems and their applicable acceptance checks. Updates (**v1.X.0**) add content and new features; patches (**v1.0.X**) provide fixes and corrections. Follow [docs/versioning.md](docs/versioning.md); keep the CMake application version and documentation aligned. Do not use this baseline designation to mark incomplete systems complete, or renumber source publications, exact rules modules, content packs or save schemas.

## Project contract

This is an existing desktop D&D character builder, not a greenfield scaffold. Extend the implementation rather than replacing working systems.

Read `PLAN.md` before changing code. It is the product specification, implementation sequence, and acceptance standard. This file supplies working instructions; it does not replace or reduce that scope. Honor explicit task instructions and applicable directory-level agent instructions. Surface conflicts instead of quietly changing requirements to match the implementation.

The stack is **C++20, CMake, Qt 6 Widgets and Print Support, nlohmann/json, Catch2, and Qt Test**. Target macOS, Windows, and Linux; development, initial native acceptance, and packaging prioritize macOS. The application operates offline with local files and requires no account. Do not replatform it as a web application or introduce a runtime service dependency.

The first-release target remains original **1981 B/X**, all seven core classes, creation and advancement through their normal level limits. Preserve the experimental **SRD 5.2.1 / 5.5e** Fighter and Wizard levels 1–3 proof while extending the newer implementation. The plan's “second edition” proof means this second supported ruleset, **not AD&D 2e**. Keep source-publication versions, module versions, content-pack versions, and save-format versions distinct.

The plan's dated review, assertion totals, catalog counts, and earlier screenshots are historical evidence, not proof about the current checkout. Inspect current code and reproduce relevant behavior before treating an issue as open or resolved. Passing tests do not by themselves establish complete rules coverage.

## Before editing

1. Read the relevant plan section, source and coverage ledgers, nearby implementation, and existing tests. Inspect the working tree and preserve unrelated or user-authored changes.
2. Identify the affected edition, public contract, content definitions, persistence behavior, and acceptance gate. Trace the actual caller-to-result path rather than fixing only a helper in isolation.
3. For a rules change, establish the source and an independently derived expected result before adjusting production code or test expectations. Keep unsupported or ambiguous rules explicit.
4. Locate the repository's documented configure, build, test, and packaging commands in its README, CMake configuration/presets, and CI workflows. Do not invent target names, executable flags, dependency paths, or a directory layout.
5. Make the smallest coherent change that addresses the task. Avoid unrelated refactors, bulk reformatting, dependency churn, or replacement of working architecture. Follow existing C++ conventions and ownership/error-handling patterns.

Use the repository's existing dependency and build setup. Keep generated build output outside source directories. For an already configured build, use `cmake --build` and the repository's registered test runner, normally CTest with `--output-on-failure`, supplying the actual build directory or preset and configuration. Confirm that the intended tests were discovered and executed; a successful command with no relevant tests is not validation.

A narrowly scoped task does not authorize completion of the entire roadmap. Conversely, do not use its narrowness to claim that an unmet release gate has passed. When blocked, preserve the evidence, report the blocker, and complete independently verifiable work within the task.

## Architecture invariants

Maintain separate CMake targets for the rules library, edition modules, content loading/persistence, desktop application, and command-line validator. Keep Qt out of the rules library and preserve the headless CLI's Qt-free dependency boundary. Do not put rules in widget event handlers or force headless validation to initialize the GUI.

Preserve the shared contracts:

- `CharacterDocument` stores identity, edition, campaign configuration, selected choices, advancement history, accepted dice results, and DM overrides.
- `ResolvedRuleset` identifies the edition implementation and exact enabled pack versions after dependency and conflict checks.
- `evaluate(document, ruleset)` deterministically returns legal/available choices, builder stages, sheet sections, diagnostics, and calculation explanations.
- `ContentPack` supplies a versioned manifest, namespaced identifiers, edition compatibility, dependencies, source references, and license metadata.

Edition modules are compiled into the application initially. Each owns its creation stages, legal choices, progression, mechanics, and sheet structure. Shared storage and GUI contracts must accommodate race-as-class, multiclassing, dual-classing, prestige classes, and powers without imposing a universal 5e model. Do not introduce these mechanics into editions that do not define them.

Keep ordinary class definitions, equipment, spells, tables, prerequisites, and supported modifiers in data packs. Their executable meaning belongs in the edition module. A genuinely new mechanic may require a C++ extension; ordinary supported content must not require bespoke hard-coded branches.

Evaluation must be deterministic and side-effect-free: no random generation, filesystem access, GUI activity, or implicit actions. Accept incomplete documents and return actionable missing/invalid-choice diagnostics. Invalid input must not become a valid-looking complete character, an unexplained default, or a crash.

Generate dice results only through explicit actions outside evaluation. Once accepted, they are document inputs. Recalculation, repainting, changing visibility, saving, and reopening must never reroll them. Keep creation and advancement inputs distinct from editable current resources.

## Content validation and resolution

Accepting mechanically active content is a commitment to interpret its supported mechanics, not merely to parse its JSON. Validate the entire imported folder, manifest, mechanical payloads, and applicable resolved references before making its rules available.

- Validate edition compatibility, exact dependencies, duplicate identifiers, conflicts, supported feature/profile bindings, progression row types and bounds, and references used by mechanics.
- Dispatch reusable behavior through supported profiles rather than literal stock content IDs. Preserve the selected record's identity, publisher, and sources. Check equivalent assumptions in subclasses, progression, and feature handlers.
- Check replacement kind, required target shape, and the mechanical role of resolved references. A same-kind replacement must still satisfy the target's supported schema; a `language` cannot replace a required combat table.
- Reject unsupported executable bindings and malformed payloads with a diagnostic identifying the offending pack/record and field or reference where possible. Do not silently omit their effects or misreport broken content as an invalid character choice.
- Keep installation/enabling failures from damaging the usable installation. Valid resolution and replacement outcomes must be independent of pack installation or input order; never use “last pack wins.”

Official, third-party, and homebrew content use the same technical interface while retaining origin and license metadata. Catalog-only and partial records are not claims of complete implementation: keep their declared coverage visible and block unsupported effects. Maintain the documented schema, templates, and validator together when changing supported definitions.

Exercise the real validator, installer, resolver, and public evaluator in integration regressions. Helper-only tests are insufficient for an acceptance/resolution mismatch.

## Source fidelity and coverage

Use the original publication for the edition/profile being implemented. Follow the existing source ledger and the source references in `PLAN.md`. The Moldvay Basic and Cook/Marsh Expert links are **partial previews**, not evidence for pages they do not contain.

For new or disputed mechanics, verify edition identity and the relevant original pages; compare overlapping sources where available. Inspect tables visually because extracted text may scramble columns. Record printed-page references, and SRD section references where applicable, with the rule and its independently calculated acceptance examples. Do not manufacture citations or describe an inaccessible page as checked.

Do not substitute a retroclone, another edition, remembered rules, or an extrapolation for verified original B/X behavior. When source access or interpretation is unresolved, record exactly what remains unsupported. A campaign ruling must remain identifiable as a ruling, with its reason, rather than becoming an undocumented global rule.

Track coverage per publication and feature using the repository's existing status conventions. Distinguish cataloged, partial, implemented, and verified coverage; explain the supported scope of each record. Generating entries, passing baseline class/level cases, or accepting a schema does not establish implementation of every mechanic or legal combination.

Retain BSD-3-Clause for application code. Track content reuse terms separately, preserve required attribution/license metadata, and bundle only material permitted by those terms. The experimental source is SRD 5.2.1 under its Creative Commons terms. Do not put book scans or copied book prose into application distributions.

## Documents, actions, and explanations

Save exact edition/module/pack identities and versions in versioned character files. Recompute derived statistics on load; do not silently upgrade a ruleset, substitute a different source version, or convert editions.

Preserve selected inputs when their source is disabled or unavailable, explain what became unavailable, and allow inspection without destroying the original file. Missing dependencies and unsupported save versions must not trigger a lossy rewrite. Retain atomic saves, recoverable autosaves, and interrupted-save recovery. Do not overwrite exact legacy module/pack versions when adding newer ones.

Use one effective campaign-option interpretation throughout evaluation, settings displays, and explicit dice actions. Resolve inheritance **per key**: an absent character key inherits its campaign default; an explicit character value, including `false`, is an override. An unrelated character option must not mask an inherited setting. Never reprocess accepted rolls merely because the effective setting changed.

Persistent actions must preview their effects, apply atomically, record accepted inputs and history, and leave the document unchanged on failure. Preserve resource costs, recovery exceptions, ownership, and applicable campaign rulings across save/reopen.

Every derived value needs reconstructable calculation steps and the sources that actually contributed. Show substituted operands, modifiers, totals, and relevant item/class contributions, not just a formula label or final number. Choice restrictions need reasoned explanations too. Use the same engine results for the GUI, sheet, and export rather than maintaining competing calculations.

DM overrides require a reason and apply only to supported choices/statistics. Display the normal calculation and effective overridden result together. Overrides cannot invent missing mechanics, bypass broken content-pack resolution, or turn unsupported rules into verified coverage.

## Desktop behavior

Preserve the three-pane Qt Widgets layout: stages on the left, current choices in the center, live character sheet on the right. Keep it resizable, keyboard-accessible, and consistent with system appearance.

Retain creation/open/save/recent-file workflows, campaign presets, source selection, and the searchable content browser with publication provenance. Keep edition-specific stages and sheet contents flowing through the shared public interfaces.

Advanced mode changes visibility only. Hiding optional-rule or override controls must preserve existing selections, reasons, and effects. Show actionable validation and “Why this value?” / “Why is this choice unavailable?” explanations without requiring users to infer the cause of an error.

Printable sheets and PDF exports must include edition identity, sources, and override annotations. Inspect all pages of representative output for clipping, pagination, and omitted information; a successfully written PDF is not visual acceptance.

## Implementation sequence and regression anchors

Unless the user explicitly assigns different work, follow the next incomplete gate below in the order specified by `PLAN.md`. Check whether the listed findings are already fixed. Keep their regressions even after closure, and retain the existing B/X and exact-version legacy SRD acceptance cases throughout.

### 1. Align accepted content with executable mechanics

Fix supported-profile dispatch, replacement compatibility, and mechanical payload validation before accepting more definitions.

Required regressions: an alternate-ID Fighter with `rulesProfile: "fighter"` retains the stock profile's class choices, features, and resources, including Second Wind, except for intentional content differences. Preserve its own identity and sources. Unsupported bindings fail explicitly. A wrong-kind replacement for `bx:combat-tables`, or a malformed required table such as missing `attackRows`, fails before availability and leaves the existing installation usable. A valid compatible same-kind replacement still works, independent of pack order. Preserve exact-version and conflict behavior.

### 2. Close B/X correctness and advancement-scope gaps

Preserve malformed memorization values while rejecting them as invalid. For example, `prepared: {"1": [123]}` is not an intentionally empty slot and must not produce `complete: true`; identify the exact slot for correction.

Regress campaign inheritance with an unrelated character option present, an inherited `rerollLowFirstHp`, and explicit per-key exceptions. Evaluation, the settings dialog, and explicit HP rolls must agree, while already accepted dice and save/reopen results stay unchanged.

The reviewed table coverage is human classes through 14, dwarf 12, elf 10, and halfling 8. **Do not redefine human level 14 as the normal B/X maximum.** Reconcile the original normal-level-limit requirement with the Expert continuation guidance in the source ledger. Distinguish verified printed tables, supported original-book continuation, and unresolved extrapolation. Keep the plan, coverage ledger, and interface consistent before declaring first-release scope complete.

### 3. Complete explanations and verify first-release delivery

Expand composite AC, skills, speed, initiative, and relevant item/class contributions into independently reconstructable arithmetic with actual sources. Include regression examples for Draconic Resilience components and Expertise ability/proficiency contributions. Preserve normal/effective override results.

Repair installed CLI content discovery: installed packs live under `share/dungeoning-a-dragon`, not an assumed accessible checkout `data/packs`. Discover installed content relative to the executable; retain precedence for the existing explicit pack-directory argument. Test a relocated installation with source data unavailable, and require missing exact versions to fail without substitution.

Run the combined automated suite for the resulting revision. Exercise the native Mac create/equip/advance/explain/override/save/reopen/export workflow, including campaign inheritance and source failures. Render and inspect every page of representative PDFs. Build a fresh distributable and verify bundled content, dependencies, signatures, and relocated GUI/CLI behavior. Inspect actual hosted macOS/Windows/Linux results rather than inferring success from workflow files. Older archives and screenshots do not satisfy this gate.

### 4. Complete the expanded SRD character lifecycle

Finish supported character-owned item mechanics, partial item records, scroll casting and higher-level checks, spell storage/absorption, persistent activation/suppression, and equipment/container/encumbrance behavior. Complete spellbook loss/backup/replacement, applicable crafting, companion ownership/state, activation costs, and source-specific recovery exceptions. Existing familiar and class-action code still needs integrated acceptance.

Validate interactions among multiclass progression, feats, spellcasting, items, attunement, forms, resources, and history. Baseline single-class states are not evidence for every legal combination or transaction. Give each supported persistent workflow an independently sourced example, a meaningful regression, and a native save/reopen check where exposed. Keep unsupported effects blocking and coverage labels accurate. Refresh PDF/package acceptance after material lifecycle changes. Do not claim full SRD coverage before this lifecycle gate is met; combat adjudication remains outside the builder.

### 5. Resume edition and supplement expansion

After the earlier gates, proceed with OD&D, Holmes Basic, AD&D 1e, BECMI and Rules Cyclopedia as distinct profiles, AD&D 2e, 3.0, 3.5, 4e, and 2014 5e. Add official and third-party supplemental publications alongside their applicable editions.

For the OD&D (1974) milestone, preserve the original class names **Fighting-Man**, **Magic-User**, and **Cleric**. Its default printed/PDF sheet must use the user-requested **Courier-style monospaced typewriter font** and **ASCII characters for lines, boxes, and section blocks**, with the appearance of a document typed in 1974. Follow the detailed requirements and every-page visual acceptance in `PLAN.md`; retain edition identity, sources, and override annotations. This sheet style is explicitly in the OD&D milestone's scope.

For each edition/publication, establish the original-source ledger and reuse terms, identify and implement new mechanics before accepting their content, use the shared lifecycle contracts, and add independent progression and save/reopen examples. Preserve the long-term coverage goal without presenting a catalog as implemented rules.

## Validation and completion

Add a focused regression for each bug or newly supported mechanic. Derive expected results independently from the applicable source, not from the implementation under test. Do not weaken assertions, delete failing acceptance cases, or mark tests skipped merely to obtain a passing suite.

Select checks by affected behavior, then run the combined suite when closing a gate or changing shared contracts:

- **Rules:** all seven B/X classes (cleric, dwarf, elf, fighter, halfling, magic-user, thief), first level, progression boundaries, and supported/source-reconciled normal maxima; ability generation/adjustments and eligibility thresholds, alignment/languages, money/equipment restrictions, XP changes, HP progression, saves, attacks, class abilities, and spells. Retain the legacy SRD Fighter/Wizard levels 1–3 cases covering species, background, skills, feats, subclasses, spellcasting, ascending AC, and proficiency through the same contracts.
- **Failure and compatibility:** incomplete drafts, malformed inputs, invalid combinations, unsupported mechanics, disabled/missing sources, dependency/conflict/replacement failures, exact-version resolution, overrides, deterministic reevaluation, and per-key campaign inheritance. Ensure pack errors and character errors remain distinguishable.
- **Persistence and actions:** save/reopen preserves choices, accepted rolls, advancement, sources, overrides, resources, and history; statistics recompute consistently. Test action failure rollback, interrupted saves, recoverable autosaves, missing dependencies, and unsupported save versions without corrupting originals.
- **Delivery:** Qt tests plus actual native Mac workflows; every-page PDF inspection; packaged and relocated GUI/CLI runs with installed data; actual hosted platform results. Report native, automated, visual, packaging, and other-platform verification separately.

Update the relevant plan status, coverage/source ledgers, schemas/templates, and user/developer documentation when behavior or evidence changes. Record the exact tested revision and any uncommitted changes, commands, environment, outcomes, and remaining gaps. Never relabel an older run as validation of a newer artifact.

A task is complete only when its requested behavior and applicable acceptance checks are satisfied, existing acceptance behavior is preserved, and any remaining scope limits are explicit. If tests, source pages, native UI, packaging, or hosted jobs could not be inspected, name that limitation rather than claiming success. Finish with a brief summary of changes, checks actually run, and outstanding blockers; distinguish implemented code from verified behavior.

## Scope boundaries

Do not add account requirements, cloud dependencies, or automatic cross-edition conversion. The explicitly requested 1974 typewritten sheet belongs to the OD&D milestone. In-app content authoring, PDF extraction, period-style sheets for other editions, full random-character generation, and NPC generation remain later features unless explicitly requested. Do not turn character-owned lifecycle mechanics into an unrequested combat adjudication system.
