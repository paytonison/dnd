# Dungeoning a Dragon: C++ Character Builder

## Summary

Build a desktop character builder with a **C++20 rules engine and a Qt 6 Widgets GUI**, supporting macOS, Windows, and Linux, with development and initial acceptance testing on the Mac. Qt’s C++ controls, model/view system, and desktop layouts suit the proposed interface. [Qt Widgets documentation](https://doc.qt.io/qt-6/qtwidgets-index.html)

The first playable release will implement **original 1981 B/X character creation and advancement for all seven core classes through their normal level limits**. A small 5.5e implementation will then demonstrate that a substantially different edition works through the same engine and GUI interfaces.

The long-term coverage target includes every requested D&D edition and both official and third-party supplements. Coverage will be recorded per publication and feature, distinguishing cataloged sources from implemented and verified rules.

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

## Source verification and delivery sequence

1. **Establish the original references.** Begin the source ledger with the verified online [1981 Moldvay Basic preview](https://d1vzi28wh99zvq.cloudfront.net/pdf_previews/110274-sample.pdf) and [1981 Cook/Marsh Expert preview](https://d1vzi28wh99zvq.cloudfront.net/pdf_previews/110792-sample.pdf). These are partial previews. Locate and inspect the remaining original pages online during implementation, checking edition identity and comparing overlapping pages. Verify tables visually because extracted text can scramble columns.
2. **Build the foundation.** Establish the C++ library, edition registry, content loader, explanations, persistence, headless validation tool, and Qt shell. The current workspace has no application implementation; setup must include Qt 6.
3. **Complete B/X.** Implement the character lifecycle and attach printed-page references to rules and independently calculated acceptance examples. Record ambiguities explicitly; unresolved mechanics remain identified until verified.
4. **Prove the second edition.** Add an experimental 5.5e module using SRD 5.2.1, covering fighter and wizard creation and advancement through level 3. Exercise species, background, skills, feats, subclass choices, spellcasting, ascending AC, and proficiency through the same public contracts. Label this coverage experimental. Wizards publishes SRD 5.2.1 for reuse under Creative Commons. [Official SRD resource](https://www.dndbeyond.com/srd)
5. **Expand coverage.** Complete the 5.5e SRD character lifecycle, then add OD&D, Holmes Basic, AD&D 1e, BECMI/Rules Cyclopedia profiles, AD&D 2e, 3.0, 3.5, 4e, and 2014 5e. Add supplemental publications alongside their applicable editions, with per-source implementation and verification records.

Retain the repository’s BSD-3-Clause license for application code. Track content licenses separately, bundle material according to its reuse terms, and keep book scans and copied book prose out of application distributions.

## Validation and defaults

- Verify every B/X class at first level, progression boundaries, and its normal maximum level. Cover ability and eligibility thresholds, XP changes, hit-point progression, equipment restrictions, saves, attacks, and spell progression.
- Test incomplete drafts, invalid combinations, source removal, pack conflicts, DM overrides, and deterministic recalculation.
- Confirm that save/reopen preserves choices, rolls, advancement, source versions, and resulting statistics. Exercise interrupted saves and missing dependencies.
- Require the second edition to work through the same document, evaluation, and GUI contracts while all B/X acceptance examples continue passing.
- Test the complete Mac workflow: create, equip, advance, inspect an explanation, apply an override, save, reopen, and export. Inspect printed output for clipping, pagination, and omitted information.
- Add build and automated-test checks for macOS, Windows, and Linux. Initial manual acceptance and application packaging prioritize macOS; report other-platform verification separately.

**Defaults:** The application works offline with local files and requires no account. Characters retain their edition; automatic conversion is outside this release. Additional content enters through structured packs. In-app content authoring, PDF extraction, period-style sheets, full random-character generation, and NPC generation remain later features.
