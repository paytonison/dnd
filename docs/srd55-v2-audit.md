# SRD rules-module 2.0.0 functional audit

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

Dated results below retain their original tested snapshots; the current documentation version does not relabel or rerun those artifacts.

Read-only review performed September 8, 2026 against the in-progress `src/editions/srd55_v2.cpp`, `src/content.cpp`, and the feature helpers called by the evaluator. Findings were sent to the implementation owners as they were identified. The owners changed code concurrently; the status below distinguishes a code correction observed in the working tree from a regression whose integrated execution still needs verification. This audit did not edit application code.

The rules reference is the [official English SRD 5.2.1](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf). Page numbers are original printed pages. File/line references identify the code inspected; function names and field paths are included because subsequent owner edits can move lines.

The already acknowledged gaps in spell-preparation replacement cadence, copying UI, broad magic-item coverage, and adventuring inventory transactions are not repeated as new findings. The review focused on incorrect legal choices, calculations, chronology, source integration, and advertised pack-version support.

## Follow-through findings corrected after the initial report

### A1 — P2: Class-feature-granted Skilled is resolved too late for dependent Expertise

**Location:** `src/editions/srd55_v2.cpp`, `run()`, the initial Skilled preloading loop around lines 250–255, `resolveClassChoices(ctx)` around line 269, and the subsequent `grants.feats` merge. The dependent Scholar choice is in `src/editions/srd55_features.cpp`, `resolveClassChoices()`'s Wizard branch.

The first pass preloads proficiencies from background/Human/advancement Skilled selections. A Skilled feat granted by **Lessons of the First Ones** is not in that initial feat list. It becomes a root `FeatSelection` only after class-choice resolution, but Scholar/Expertise has already validated against the earlier proficiency state.

A legal **Warlock 2 → Wizard 2** can therefore be rejected: Lessons grants Skilled/Arcana at character level 2, and Scholar selects Arcana when Wizard 2 is reached at character level 4. Arcana exists in the final feat processing but was absent when the prerequisite was checked. This is distinct from the previously reported acceptance of a proficiency gained too late.

**Required correction:** materialize feature-granted feat proficiencies during the grant-resolution pass, with the actual grant's character level, before evaluating dependent non-retrainable proficiency choices. Do not assign every class-feature feat the final character level.

**Regression fixture:** initial Warlock skills exclude Arcana; background/species do not grant it; Warlock 2 takes Lessons → Skilled with Arcana and two other new proficiencies; Wizard 2 takes Scholar/Arcana. The character must validate. A control case in which Arcana is obtained after the Scholar event must remain invalid.

**Source:** Lessons grants an Origin feat on [page 73](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=73); Skilled grants three proficiencies on [page 87](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=87); Scholar requires an existing proficiency when obtained on [page 78](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=78).

**Follow-through status:** corrected with chronology-stamped provisional grants before class-feature prerequisites. Feature regressions cover both acquisition orders, and the integrated complete-character regression validates earlier Lessons skills followed by Bard Expertise.

### A2 — P2: Set-based class-feat merging drops a legitimate repeated Skilled grant

**Location:** `src/editions/srd55_v2.cpp`, `run()`, `for (const auto& id : grants.feats) if (!ownedFeats.contains(id)) ...`; `src/editions/srd55_v2_internal.hpp`, `FeatureResult::feats` is a set of IDs.

A **Human Warlock 2** may take Skilled as the Human Origin feat and Skilled again through one Lessons of the First Ones invocation. Skilled is repeatable, and the Lessons rule requiring different feats between repeated Lessons invocations is not violated: there is only one Lessons instance. The current class-grant merge finds Skilled already owned and discards the second grant. The builder consequently exposes one three-proficiency choice group instead of two independent groups.

The same problem occurs when an advancement feat and Lessons both legally grant Skilled. Deduplicating nonrepeatable ownership is not a representation for repeatable feat instances.

**Required correction:** pass feat-grant records with stable source/instance identity from the feature layer, preserving valid multiplicity. Validate repeatability before converting ownership to any convenience set. Retain separate choice paths for each Skilled instance.

**Regression fixture:** Human Warlock 2, Human Skilled choices A/B/C, Lessons Skilled choices D/E/F, all six distinct and initially untrained. All six proficiencies must be granted and saved independently. Adding an unrelated nonrepeatable duplicate must still fail.

**Source:** Human Versatile on [page 86](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=86), Lessons on page 73, and Skilled's explicit repeatability on page 87.

**Follow-through status:** corrected with source-instance `featGrants` records. The integrated Human Warlock regression completes with six distinct proficiencies and two retained choice paths.

### A3 — P2: Counted preparations include spells already always prepared by that class

**Location:** `src/editions/srd55_v2.cpp`, `run()`, `preparedOpts`/`picks(..., preparations)` around lines 567–575. Automatic class-feature spell grants are already available in the separate grant collections but are not excluded from the counted preparation choices.

A **Life Cleric 3** receives Aid, Bless, Cure Wounds, and Lesser Restoration automatically and can prepare six ordinary Cleric spells. Selecting Cure Wounds among those six currently uses one of the six counted slots even though the class feature says the automatic spell does not count against the limit. The character can be marked complete with only nine distinct available spells instead of the six selectable plus four automatic spells. Spell Mastery, Signature Spells, and other subclass spell grants have the same interaction.

This does not require preparation-history enforcement: it is a current-state counting problem.

**Required correction:** exclude same-class automatic preparations from that class's counted options, or count only nonautomatic entries and explain why the automatic spell costs no preparation. Keep independent casting profiles from other sources where they matter; do not indiscriminately collapse a Wizard/Intelligence spell with a Magic Initiate/Wisdom grant.

**Regression fixture:** Life Cleric 3 must choose six non-domain spells; its four domain spells remain usable without consuming any of those six selections. A prepared-spell option for an automatic domain spell should either be unavailable with an explanation or explicitly not count. Add Wizard 20 Signature Spell and Sorcerer subclass controls.

**Source:** class-feature always-prepared spells do not count against ordinary preparations in the Cleric Spellcasting rules on [pages 36–37](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=36), Life Domain's grant on [page 40](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=40), and the Wizard rule on [page 78](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=78).

**Follow-through status:** corrected by excluding same-class automatic preparations from counted options. The Life Cleric option regression and all 240 complete public-character states pass with the corrected allowance.

## Findings corrected in the working tree during this audit

These findings remain documented so their regression cases survive the immediate repair. “Correction observed” means the relevant code path was reread; it does not substitute for the final integrated test run.

| ID / original priority | Defect and affected code | Published rule / concrete regression | Correction observed |
|---|---|---|---|
| **B1 / P1** | `srd55_v2.cpp`, Wizard Savant loop, originally lines 524–527, granted an extra Evocation spell at every odd Wizard level after 3, including **19**. | [SRD 82](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=82) grants later spells only when a **new spell-slot level** becomes available; ninth-level slots arrive at Wizard 17. Wizard 20 should have **44 normal + 9 Savant = 53** no-copy spells, with no `/spellcasting/wizard/savant/19` choice. | The owner changed the condition to compare the highest slot level with the preceding class-level row and added a regression for the absence of a level-19 grant. |
| **B2 / P2** | `srd55_v2.cpp`, `abilityIncrease()`, originally line 198, applied the cap check to every ability even when its increase was **zero**. | [SRD 87](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=87) limits that feat's increases. Ordered Wizard 3 → Fighter 16 reaches character 19, takes a boon increasing Strength 20→21, then Wizard 4 at character 20 takes ASI +2 Wisdom. Untouched Strength 21 must not invalidate the Wisdom increase. | The condition now requires `delta > 0`; a regression preserves Strength 21 while increasing another ability. |
| **B3 / P2** | `srd55_v2.cpp` initialized `ownedFeats` before class-feature feat grants; later grants were silently deduplicated. `srd55_features.cpp`'s style/ Lessons choices did not know pre-existing feat ownership. | [SRD 87](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=87): a feat can be taken once unless repeatable. Fighter 4 cannot take Defense both as its class Fighting Style and as its advancement feat. Fighter/Ranger cannot independently choose the same nonrepeatable style. Lessons cannot grant an already owned nonrepeatable Origin feat. | `Context::feats` now receives existing ownership; style choices exclude it and prior style grants; Lessons excludes owned nonrepeatable feats. The additional same-list Magic Initiate restriction was also added. Valid repeatable instances still need A2's follow-through. |
| **B4 / P2** | `srd55_v2.cpp`, `toolChoices()` around lines 167–173 and starting-kit choice handling around 353–357, read only singular `category`; Monk data uses `categories`. The kit's `matchesProficiency` was ignored. | [SRD 49](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=49): Monk chooses an Artisan's Tool or Musical Instrument, and kit A supplies the chosen tool/instrument. Thieves' Tools/Gaming Sets must not appear as legal Monk proficiency choices, and the kit item must match an acquired eligible proficiency. | Both singular/array category forms are filtered; stable fallback choice IDs replace an empty path token; kit matching now checks tool proficiency. |
| **B5 / P2** | `srd55_v2.cpp`, weapon ownership filtering originally around line 370, required a weapon ID directly in inventory and ignored an owned focus's `weaponProfile`. Version-2 Wizard kit A contains the single physical staff focus, not a duplicated Quarterstaff item. | [SRD 77](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=77) gives the staff focus; [SRD 96](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=96) says that staff is also a Quarterstaff. Wizard/Druid staff owners must be able to select its attack without buying another weapon. | Ownership now recognizes linked weapon profiles. Future inventory-instance/weight work should continue to treat a profile as part of its physical item, rather than two physical objects. |
| **B6 / P2** | `srd55_features.cpp`, `knownWizard()`, originally line 23, unioned all raw spellbook/Savant paths, including retained future levels and invalid copying records. | [SRD 79](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=79): Spell Mastery requires a spell in the current book. Wizard 18 with Shield only under `/spellcasting/wizard/spellbook/19` must not select it for Mastery. Keeping a future selection for inspection must not grant its effect early. | The helper now bounds acquisition levels, checks spell/list/rank, gates Savant levels, and checks copying eligibility/cost/time. |
| **B7 / P2** | `srd55_v2.cpp`, `innate()` originally around lines 472–478 emitted `freeUses` only into temporary spell profiles; no resource was created, and the print profile discarded the free-use metadata. | [SRD 85–86](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=85): Elf/Tiefling level-3/5 spells each have one free casting per Long Rest; Forest Gnome Speak with Animals has proficiency-bonus free castings. High Elf 5 needs two distinct daily permissions. | Innate spell grants now create source-stable resource IDs and retain casting/recharge information in printed profiles. Refresh must preserve spent uses. |
| **B8 / P2** | `srd55_v2.cpp`, class-cantrip profile creation and final `printedProfiles` fallback omitted class feature save/attack bonuses, although ordinary prepared-spell profiles included them. | [SRD 65–66](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=65): Innate Sorcery raises the Sorcerer spell save DC. Sorcerer 1, Cha 16, PB +2, active Innate Sorcery must show **DC 14** for Acid Splash and Burning Hands, consistently with the class DC. | The owner added a common pass applying owner-specific DC/attack bonuses to all class casting profiles, including cantrips. |
| **B9 / P2** | All multiclass skill grants and later Skilled choices were available before `resolveClassChoices()` checked earlier non-retrainable Expertise. Scholar used final proficiency membership. | [SRD 78](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=78): Wizard 2's Scholar requires existing proficiency. Wizard 2 → Bard 1 cannot choose Scholar/Religion when Religion is obtained only from Bard at character 3. A control with Religion already known at the Scholar event must pass. | Root/feature owners added skill-acquisition levels and class-level event mapping, and Expertise checks now compare acquisition time with grant time. A1 covers the opposite case where a valid earlier class-feature feat has not yet been materialized. |
| **B10 / P2** | `srd55_features.cpp`, Pact of the Tome's `alreadyPrepared` set originally included only ordinary `/spellcasting/<class>` choices, ignoring Origin/automatic feature grants. | [SRD 74](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=74) excludes already-prepared spells from the Book of Shadows grant. A Sage Warlock with Magic Initiate (Wizard) Detect Magic cannot choose Detect Magic again as a Tome ritual. | The owner now supplies existing prepared grants in context and includes feature/automatic grant sources in Tome exclusions. |
| **B11 / P2** | `src/content.cpp`, `installPack()` originally lines 174/188, resolved dependencies only for `moduleVersions.front()`, even though `loadPack()` validates shapes for every advertised module. | The plan requires the entire pack to validate before availability. An armor add-on advertising modules `[1.0.0, 2.0.0]` but depending on `srd55-core 1.0.0` must not install merely because the v1 resolution passes; its v2 dependency closure is incompatible. Reversing the advertised version order must not change acceptance. | `installPack()` now resolves the pinned closure for each advertised module version and rejects any resolution errors before publication. |

## Regression details to retain

The original baseline and independent content tests must remain separate from these integration regressions. Avoid fixtures that become “valid” by granting more spells or proficiencies than the source allows.

1. **Wizard 19/20 Savant:** assert exactly eight Savant choice groups: class level 3 plus 5/7/9/11/13/15/17. The level-3 group grants two spells, all others one. Assert no retained future group contributes to the current spellbook or Spell Mastery eligibility.
2. **Over-cap untouched score:** use a legal character-level-19 boon followed by a different class's character-level-20 ASI. Assert both the unchanged score of 21 and the increased second ability; assert no `feat.cap` error.
3. **Cross-source feat ownership:** cover class style plus ASI, two class style sources, Lessons plus an existing nonrepeatable Origin feat, and same-list versus different-list Magic Initiate. Also cover the legitimate repeated Skilled case from A2 so duplicate prevention does not erase repeatable grants.
4. **Proficiency chronology:** test both directions: a later proficiency cannot satisfy earlier Scholar/Expertise; a genuinely earlier class-feature-granted Skilled proficiency must satisfy a later choice. Use characters whose background, initial skills, and species do not accidentally grant the target skill.
5. **Caster profile consistency:** evaluate a Sorcerer with Innate Sorcery off/on and compare the class DC with every matching class-owned cantrip and leveled-spell profile. Origin spells using another source must retain the appropriate independent ability and bonuses.
6. **Origin resources:** compare High Elf 2/3/5, Forest Gnome at total character level 5, and a multiclass whose total level reaches a species threshold before any class does. Resource IDs and current spending must survive save/reopen without resetting.
7. **Automatic preparations:** count distinct automatic and selected spells with source ownership. Life Cleric 3 should support six non-domain choices plus four domain spells; automatic grants must not consume ordinary preparation capacity.
8. **Whole-pack preflight:** use a temporary add-on and temporary installation directory. Assert rejection before the destination is published when any advertised module's dependency closure is invalid; preserve the original source directory and existing installed packs.

## Checks that matched the inspected rules

The inspected root paths correctly distinguish total character proficiency from individual class levels, retain initial-class saving throws/training, use the newly chosen class's Hit Die after first character level, apply the normal Constitution modifier across HP history, separate Pact slots from ordinary Spellcasting slots, and derive preparation spell-level eligibility from each class's own slot progression. These observations are limited to the reviewed paths and do not replace the complete class/level and GUI acceptance matrix.

The multi-half-caster rounding interpretation when both Paladin and Ranger have odd levels was already flagged in the expansion handoff. This audit does not label a disputed reading as a proven defect without a more explicit primary-source clarification. The source-backed Ranger 4/Sorcerer 3 example and a single odd half-caster contribution remain useful unambiguous regressions.

## Integrated follow-through verification

After the corrections above, `ctest --test-dir build-dev -V` passed 34,980 assertions across 76 core cases and all 14 QtTest entries. The log is `output/validation/ctest-v2.txt`. This confirms the tested corrections and does not close the separately acknowledged full-lifecycle gaps in `v2-progress.md`.
