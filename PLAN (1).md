# Dungeoning a Dragon: C++ Character Builder

## Summary

Build a desktop character builder with a **C++20 rules engine and a Qt 6 Widgets GUI**, supporting macOS, Windows, and Linux, with development and initial acceptance testing on the Mac. Qt’s C++ controls, model/view system, and desktop layouts suit the proposed interface. [Qt Widgets documentation](https://doc.qt.io/qt-6/qtwidgets-index.html)

The first playable release will implement **original 1981 B/X character creation and advancement for all seven core classes through their normal level limits**. A small 5.5e implementation will then demonstrate that a substantially different edition works through the same engine and GUI interfaces.

The long-term coverage target includes every requested D&D edition and both official and third-party supplements. Coverage will be recorded per publication and feature, distinguishing cataloged sources from implemented and verified rules.

## Current implementation review — September 8, 2026

The codebase is substantially on its way to fulfilling this specification. The shared engine, desktop builder, persistence, original B/X implementation, and experimental second-edition proof exist. Development has also progressed into the expanded SRD lifecycle. The next work is to close correctness and acceptance gaps in those foundations before adding further editions.

| Area | Current evidence | Remaining gate |
|---|---|---|
| Architecture | C++20/CMake targets, Qt-independent rules, edition-neutral document, deterministic evaluation, generic stages and actions | Ensure every accepted content definition has an executable, validated interpretation |
| Original B/X | All seven classes, independent progression expectations, source ledger and original-page spot-checks | Fix invalid-input and campaign-option inconsistencies; reconcile the human level-14 coverage limit with the original maximum-level requirement |
| Experimental SRD proof | Fighter and Wizard levels 1–3 use the same engine, GUI, save, and sheet contracts; exact legacy version remains available | Preserve these acceptance cases while expanding the newer module |
| Expanded SRD 2.0.0 | All 12 classes through 20, 240 baseline single-class states, focused multiclass cases, inventory/history/resource/companion/action work | Complete remaining persistent lifecycle mechanics and validate combinations, imported profiles, and native workflows |
| Desktop and persistence | Three panes, explanations, reasoned overrides, sources, atomic saves, recovery, exact versions, PDF export | Improve calculation detail, fix inherited campaign-option behavior, and repeat native/PDF/package acceptance for the current implementation |
| Platforms and distribution | Fresh local Mac build and automated suite pass; three-platform CI workflow exists | Verify hosted platform results and a new distributable; repair installed CLI content discovery |
| Other editions and supplements | Coverage ledger and common pack interface exist | The remaining named editions and supplemental publication coverage are still unimplemented |

The fresh review build of revision `24e861f` (before this plan-only edit), in `/tmp/dnd-spec-review-20260908`, passed **36,640 assertions in 141 core test cases** and **all 23 QtTest entries, including setup and cleanup**. The CLI linked only system C++/system libraries, with no Qt dependency. This is current local build and automated-workflow evidence; it does not replace native manual acceptance, visual PDF inspection, hosted Windows/Linux results, or verification of a packaged release. Earlier validation counts in other documents describe earlier milestones.

Passing baseline tests does not establish complete content coverage. Targeted review probes found that an accepted alternate Fighter profile silently loses its class features, an accepted replacement can substitute a language for a required B/X table, and a malformed memorization entry can be reported as a complete character. These findings drive the next steps below. The original requirements remain the acceptance standard.

## Architecture and interfaces

- **Build structure:** Use CMake with separate targets for the rules library, edition modules, content loading and persistence, desktop application, and a small command-line validation tool. Use Qt 6 Widgets and Print Support for the GUI and printing. Keep Qt out of the rules library. Use JSON for content and saves, with nlohmann/json for serialization, Catch2 for core tests, and Qt Test for interface tests.
- **Edition modules:** Compile edition implementations into the application initially. Each module defines its own creation stages, legal choices, progression rules, calculations, and sheet structure. Shared character storage must accommodate race-as-class, multiclassing, dual-classing, prestige classes, powers, and other edition-specific concepts without imposing a universal 5e character model.
- **Rules versus content:** Store class definitions, equipment, spells, progression tables, prerequisites, and supported modifiers in data packs. Implement their meaning in the edition module. A new mechanic can require a C++ extension; ordinary content additions should use existing supported definitions.

The main contracts will be:

| Contract | Responsibility |
|---|---|
| `CharacterDocument` | Stores identity, edition, campaign configuration, selected choices, advancement history, accepted dice results, and DM overrides. |
| `ResolvedRuleset` | Identifies the edition implementation and exact enabled content-pack versions after dependency and conflict checks. |
| `evaluate(document, ruleset)` | Deterministically returns available choices, builder stages, sheet sections, validation messages, and calculation explanations. |
| `ContentPack` | Provides a versioned manifest, namespaced content identifiers, supported edition, dependencies, source references, and license metadata. |

Evaluation must accept incomplete characters and explain missing or invalid choices. Random rolls, file access, and GUI activity occur outside evaluation. Accepted roll results become saved inputs, so reopening a character or refreshing the interface never rerolls it.

Each derived value carries its calculation steps and source references. The interface uses those results for both “Why this value?” and “Why is this choice unavailable?”

## First release

**Original B/X implementation**

- Implement ability generation and permitted adjustments, class eligibility, alignment, languages, starting money, equipment, armor and attacks, saving throws, class abilities, spells, experience adjustments, and advancement.
- Support cleric, dwarf, elf, fighter, halfling, magic-user, and thief, including their distinct progression and level limits. Use B/X’s own combat tables and terminology.
- Support creation at first level and higher levels, recording the necessary advancement choices and hit-point results. Separate creation and advancement inputs from any editable current resources.
- Expose published optional rules through campaign settings. Keep house rules identifiable as house rules.

**Desktop interface**

- Use the specified three-pane layout: building stages on the left, current choices in the center, and a live character sheet on the right.
- Provide character creation, opening, saving, recent files, campaign presets, source selection, and a searchable content browser showing the publication behind each option.
- Use a resizable desktop layout, system appearance, keyboard navigation, and clear validation messages.
- Include an Advanced mode for optional rules and DM overrides. Turning Advanced mode off changes visibility only; it preserves existing selections.
- Allow explicit overrides of supported choices and calculated statistics, with a required reason. Show the normal calculation and effective result together. Overrides cannot supply missing rules or repair broken content packs.
- Export a clean printable character sheet and PDF, including the edition, sources, and override annotations.

**Content and persistence**

- Import structured content-pack folders containing a manifest and JSON data. Provide a documented schema, templates, and a validator. Validate the entire pack before making it available.
- Treat official, third-party, and homebrew packs through the same technical interface while retaining their publisher and origin metadata.
- Detect incompatible editions, missing dependencies, duplicate identifiers, and conflicting replacements. Installation order must never silently determine which rule applies.
- Save versioned character files with exact module and pack versions. Recompute derived statistics when loading. Preserve affected selections when a source is disabled and explain what became unavailable.
- Use atomic saves and recoverable autosaves. Missing dependencies or unsupported save versions must preserve the original file and allow inspection without silently substituting rules.

## Source verification

Maintain the existing source ledger, beginning with the verified online [1981 Moldvay Basic preview](https://d1vzi28wh99zvq.cloudfront.net/pdf_previews/110274-sample.pdf) and [1981 Cook/Marsh Expert preview](https://d1vzi28wh99zvq.cloudfront.net/pdf_previews/110792-sample.pdf). These are partial previews. For new or disputed mechanics, inspect the remaining original pages, check edition identity, and compare overlapping pages. Verify tables visually because extracted text can scramble columns. Attach printed-page references to rules and independently calculated acceptance examples; keep ambiguities and unverified mechanics explicit.

The experimental second edition uses SRD 5.2.1, published for reuse under Creative Commons. Preserve exact legacy module/pack versions while extending the newer implementation. Its Fighter/Wizard creation and advancement proof through level 3 must continue exercising species, background, skills, feats, subclass choices, spellcasting, ascending AC, and proficiency through the same public engine and GUI contracts. Keep this coverage labeled experimental. [Official SRD resource](https://www.dndbeyond.com/srd)

Retain the repository’s BSD-3-Clause license for application code. Track content licenses separately, bundle material according to its reuse terms, and keep book scans and copied book prose out of application distributions.

## Next implementation steps

Execute the following in order. Each step should retain the existing B/X and legacy SRD acceptance cases. Scope completion is determined by the requirements and source evidence, not by test totals or catalog size.

### 1. Make content validation agree with the mechanics the engine executes

- **Fix supported-profile dispatch.** The SRD validator accepts a class with `rulesProfile: "fighter"`, but feature evaluation dispatches through literal `srd55:fighter` identifiers. A copy of the Fighter with only its ID/name changed currently validates and evaluates as complete while losing class features and Second Wind. Resolve class behavior through the supported profile and preserve the selected content's identity and sources. Explicitly reject unsupported bindings until their interpretation exists; a valid-looking character must not silently lose mechanics. Inspect the equivalent assumptions in subclasses, progression, and feature handlers.
- **Validate replacement compatibility.** `resolveRuleset` currently permits a `language` entry to replace `bx:combat-tables`; both pack validation and installation succeed, then character evaluation fails on missing `attackRows`. Check replacement kind and supported target shape, and validate resolved references for their required mechanical role. Report the offending pack/record before installation or enabling it, rather than reporting invalid character inputs.
- **Validate mechanical payloads, not just container shapes.** Check supported feature identifiers, progression row types and bounds, and relevant references. Ordinary homebrew and third-party content must be able to reuse supported definitions; unsupported mechanics must produce an explicit validation error.

Acceptance: exercise the actual validator, installer, resolver, and public evaluator. An alternate-ID Fighter using the supported profile must retain the same class choices, features, and resources as the original, apart from intentional content differences. A wrong-kind or malformed required-table replacement must fail before availability and leave the existing installation usable. A valid same-kind replacement must still work, with outcomes independent of pack order. Add focused regressions for these observed failures and preserve exact-version/conflict behavior.

### 2. Close the remaining B/X correctness and scope gaps

- **Reject malformed memorization selections while preserving them.** `prepared: {"1": [123]}` currently becomes “Unmemorized” with `complete: true`. Distinguish an intentionally empty slot from an invalid saved value; report the exact slot and retain its input for correction.
- **Use one effective campaign-option interpretation.** The engine falls back from each absent character-option key to its campaign default. The HP roller and campaign editor instead select the entire character options object when it exists. A character with an unrelated option can therefore lose an inherited `rerollLowFirstHp` setting. Make evaluation, displayed campaign settings, and explicit dice actions agree, without rerolling existing accepted results.
- **Reconcile the human progression boundary.** Current B/X coverage stops at the full printed tables: human classes 14, dwarf 12, elf 10, halfling 8. The original requirement says normal level limits; the source ledger separately records Expert guidance beyond human level 14. Keep this requirement open until the intended B/X profile is explicitly reconciled with that cutoff. Distinguish verified table coverage, supported original-book continuation rules, and unresolved extrapolation; do not silently redefine level 14 as the normal human maximum or substitute another edition's rules.

Acceptance: malformed spell values produce actionable validation; campaign defaults and per-key character exceptions behave identically in evaluation, the settings dialog, and explicit rolls. Previously accepted dice and save/reopen results remain stable. Record the supported advancement boundary and any remaining source decisions consistently in the plan, coverage ledger, and interface before declaring the first-release scope complete.

### 3. Finish calculation explanations and validate the first-release delivery

- **Show the arithmetic and its actual sources.** Calculations consistently carry explanation fields, but some SRD results only describe a formula. Expand composite AC, skills, speed, initiative, and relevant item/class contributions to show substituted values and references for the rules that contributed. For example, explain the components of Draconic Resilience instead of only “Draconic Resilience = 17,” and show ability/proficiency contributions to Expertise. Preserve the normal/effective override distinction.
- **Repair installed CLI content discovery.** The CLI currently defaults to the source checkout's compiled-in `data/packs` path even though CMake installs packs under `share/dungeoning-a-dragon`. Discover packaged/installed content relative to the executable while retaining the explicit pack-directory argument. Test a relocated installation with the source data unavailable to that process.
- **Verify the current delivery.** After the fixes, run the combined automated suite and update validation milestones with the exact tested revision. Exercise the native Mac create/equip/advance/explain/override/save/reopen/export workflow, including campaign inheritance and source failures. Render and inspect every page of representative outputs. Rebuild the distributable and verify its bundled content, dependencies, signatures, and relocated runtime. Inspect actual hosted macOS/Windows/Linux job results; keep unexecuted platforms explicitly unverified.

Acceptance: explanation totals can be independently reconstructed from displayed steps and sources; packaged GUI and CLI operate with their installed data; an explicit CLI pack directory takes precedence, and missing exact versions fail without substitution. Native and PDF evidence applies to the newly built artifact. A previous archive or older screenshot does not satisfy this gate.

### 4. Complete the expanded SRD character lifecycle

- Finish supported character-owned item mechanics, including the relevant partial records, scroll casting and higher-level checks, spell storage/absorption, persistent activation/suppression state, and general equipment/container/encumbrance behavior. At review, the magic-item parent catalog contains **45 implemented, 14 partial, and 199 cataloged records**; these statuses describe the declared scope of each record, not complete encounter automation.
- Complete persistent spellbook loss, backup and replacement, applicable crafting, and remaining companion ownership/state workflows. Existing familiar and class-action implementations need integrated acceptance before being treated as final coverage.
- Complete activation costs and source-specific recovery exceptions that affect persistent character state. Explicit actions must preview, apply atomically, record accepted inputs/history, and preserve the document on failure. Preserve the identified campaign ruling and reason where the source leaves an interpretation open.
- Validate interactions among multiclass progression, feats, spellcasting, items, attunement, forms, current resources, and history. Baseline coverage of 240 class/level states is valuable but does not prove every legal combination or transaction.

Acceptance: give each supported persistent workflow an independently sourced example, a meaningful regression, and a native save/reopen check where the interface exposes it. Retain clear per-record coverage and blocking diagnostics for unsupported effects. Complete this character lifecycle before claiming full SRD coverage; combat adjudication remains outside the builder unless expressly added to scope. Refresh PDF/package acceptance after material lifecycle changes.

### 5. Resume the original edition and supplement expansion sequence

After the preceding gates, add **OD&D, Holmes Basic, AD&D 1e, BECMI/Rules Cyclopedia as distinct profiles, AD&D 2e, 3.0, 3.5, 4e, and 2014 5e**. Add supplemental publications alongside their applicable editions. Preserve the original long-term coverage goal.

For each edition or publication, establish its original-source ledger and reuse terms, identify new mechanics before accepting content packs, implement its lifecycle through the shared contracts, and add independent progression and save/reopen examples. Exercise race-as-class, multiclassing, dual-classing, prestige classes, and powers only under the edition that defines them. Cataloged publications must remain distinct from implemented and verified rules. Keep the application offline, preserve saved edition identity, and avoid automatic cross-edition conversion.

## Validation and defaults

- Verify every B/X class at first level, progression boundaries, and its normal maximum level. Cover ability and eligibility thresholds, XP changes, hit-point progression, equipment restrictions, saves, attacks, and spell progression.
- Test incomplete drafts, invalid combinations, source removal, pack conflicts, DM overrides, and deterministic recalculation.
- Confirm that save/reopen preserves choices, rolls, advancement, source versions, and resulting statistics. Exercise interrupted saves and missing dependencies.
- Require the second edition to work through the same document, evaluation, and GUI contracts while all B/X acceptance examples continue passing.
- Test the complete Mac workflow: create, equip, advance, inspect an explanation, apply an override, save, reopen, and export. Inspect printed output for clipping, pagination, and omitted information.
- Add build and automated-test checks for macOS, Windows, and Linux. Initial manual acceptance and application packaging prioritize macOS; report other-platform verification separately.

**Defaults:** The application works offline with local files and requires no account. Characters retain their edition; automatic conversion is outside this release. Additional content enters through structured packs. In-app content authoring, PDF extraction, period-style sheets, full random-character generation, and NPC generation remain later features.
