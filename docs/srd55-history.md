# SRD 5.2.1 acquisition and replacement history

The expanded SRD module keeps character creation editable until the player explicitly chooses **Accept character and start history**. Acceptance requires a complete character under its exact module and content-pack versions. It does not convert an older character, silently accept a draft, or reinterpret a version-1 save.

After acceptance, creation and advancement selections become read-only in the builder. The sheet and explanations remain live. Name, current resources, current equipment selection, and experience bookkeeping remain separate from those locked choices. The player changes a tracked selection through a source-specific action or begins a new level, fills its newly available choices, and commits it. This follows the separation between initial creation, later class levels, and class-specific replacement rules in [SRD pages 19–26](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=19).

## Commands and normal workflow

The generic UI displays `Evaluation.actions`; it does not implement a second set of edition rules. Action fields retain their normal select, multiselect, or integer controls. They write `CharacterCommand.inputs`.

| Command | Inputs and effect |
|---|---|
| `srd55.history.accept-baseline` | No inputs for most characters. A Warlock also declares `/invocationLevels/N`: the actual Warlock class level at which each current invocation slot was acquired. Validates and records the complete baseline. |
| `srd55.history.begin-advance` | `/classId` identifies the class gaining the next character level. Preserves every earlier class choice and begins a pending advancement. |
| `srd55.history.commit` | No inputs. Requires a complete valid proposal, validates its changes against the acquisition/replacement permissions, and records the accepted result. |
| `srd55.history.cancel` | No inputs. Restores the previous accepted choices. Newly appended dice evidence is retained. |
| Generated `srd55.history.replace.…` | `/value` contains the selected replacement, or the complete revised multiselect list. The ID and actual legal choices come from `Evaluation.actions`. A complete valid replacement commits atomically. If it introduces required dependent choices, such as a newly chosen invocation's feat or Book of Shadows spells, it opens a pending change so those choices can be completed. |

A pending operation unlocks only its permitted existing choices and newly active fields. Earlier ability rolls, class acquisition order, feats, learned Wizard spellbook entries, and fixed feature choices remain locked. Increasing character level still caps at 20; multiclass prerequisites are evaluated against the ordinary character progression. A rest or copying transaction cannot run during pending history work.

An action rejection returns the original document. A pending proposal that cannot be completed can be canceled. Evaluation itself neither spends resources nor rolls dice, advances levels, changes preparation, or accepts a proposal.

## Published replacement permissions

The module uses each class pack's verified `casting.cantripChange` and `casting.preparationChange` fields. The following additional groups are explicit rules implemented by the history module.

| Selection | Permission and shared limit | Original source |
|---|---|---|
| Bard cantrips and prepared spells | One cantrip and one prepared spell when gaining a Bard level; the two limits belong to separate features. | [32](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=32) |
| Cleric cantrips / preparation | One cantrip after a Long Rest; a revised complete preparation list after a Long Rest. | [36–37](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=36) |
| Druid cantrips / preparation | One cantrip upon a Druid level; a revised complete preparation list after a Long Rest. | [41–42](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=41) |
| Paladin/Ranger preparation | One spell after a Long Rest. Blessed Warrior/Druidic Warrior cantrips can change one at the owning class's level gain. | [54](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=54), [58–59](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=58) |
| Sorcerer cantrips, prepared spells, and Metamagic | One change in each respective group upon a Sorcerer level. New Metamagic grants are additions rather than extra replacement permissions. | [65–67](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=65) |
| Warlock cantrips / prepared spells | One of each upon a Warlock level. | [71–72](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=71) |
| Warlock invocations | One existing invocation upon a Warlock level. Replacing a prerequisite required by a retained invocation is rejected. Newly gained slots and a permitted replacement have separate acquisition records. | [71–74](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=71) |
| Mystic Arcanum | One existing arcanum upon a Warlock level, shared across its spell-level choices; the new spell must remain at the same spell level. | [72](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=72) |
| Wizard cantrips / preparation | One cantrip after a Long Rest; a complete preparation list after a Long Rest. Memorize Spell at Wizard 5 allows one preparation replacement at a Short Rest. | [78–79](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=78) |
| Wizard Spell Mastery | One of the two selected spells after a Long Rest, shared across its level-1 and level-2 selections. Spellbook membership, spell level, and action casting time remain required. | [79](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=79) |
| Magic Initiate | One spell per character-level gain for each separate feat instance, shared between its cantrips and level-1 spell. Casting ability and spell list are fixed by the accepted feat grant. | [87](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=87) |
| High Elf cantrip | One replacement after a Long Rest. | [84](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=84) |
| Barbarian/Fighter Weapon Mastery | One selected weapon after a Long Rest. | [29](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=29), [48](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=48) |
| Paladin/Ranger/Rogue Weapon Mastery | A revised selection after a Long Rest. | [54](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=54), [58–59](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=58), [62](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=62) |
| Fighter's initial Fighting Style | One replacement upon gaining a Fighter level. Champion's additional style has no separate published replacement grant. | [47, 49](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=47) |
| Lore Magical Discoveries | One of the two spells upon a Bard level. | [35](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=35) |
| Druid known forms / Circle land | One known form after a Long Rest; land may change after a Long Rest. Newly unlocked form slots are advancement choices. | [42](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=42), [46](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=46) |
| Hunter's Prey / Defensive Tactics | The selected option can change after a Short or Long Rest. | [61](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=61) |
| Book of Shadows spells / Fiendish Resilience | A revised Book after a Short or Long Rest; resistance can change after a Short or Long Rest. Prepared-spell exclusions and legal damage types remain enforced. | [74](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=74), [76](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=76) |
| Pact weapon | An explicit feature action changes the bonded/conjured weapon; normal Pact of the Blade eligibility still applies. | [74](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=74) |

“Any” rest-based selection means one accepted revision of that group, with any legal number of entries changed. It is not a stockpile of unlimited later revisions. Each permission belongs to the latest recorded rest/advancement trigger; recording another trigger replaces that window. Separate groups may be completed in a pending proposal together, while their shared budgets remain enforced. This models the character-sheet selection workflow, not a combat action-economy clock.

Fixed choices have no inferred retraining permission. Changing a feature's retained parameter without replacing the owning feature does not grant a free respec. Unrelated choices remain unchanged when a replacement is accepted.

## History format and integrity checks

History events live in `CharacterDocument.advancement` and have an SRD-owned `kind` beginning with `srd55.history.`. Existing migration records and other edition-owned events remain intact. The format is versioned independently within each event.

The baseline contains the character/module identity, accepted validation snapshot, explicitly supplied acquisition declarations, and acquisition map. Subsequent records contain monotonically increasing sequence numbers, before/after snapshots, their operation or trigger, and the pending operation they commit or cancel. A small `campaign.srd55History` marker detects accidental removal of the entire ledger while retaining the character.

Replay verifies:

- One baseline precedes all history events; event versions and sequence order are supported.
- Each event begins from the prior accepted choices, and previously accepted dice evidence is preserved.
- A rest does not alter character choices, and copying only appends its validated Wizard copying record.
- Advancement adds exactly one class level and retains the prior class chronology.
- Earlier fixed fields stay unchanged; replacement counts use the owning class/character trigger and shared budget.
- Committed snapshots remain legal under the exact loaded ruleset, including dependent invocation and spell prerequisites.
- Current tracked choices equal the latest accepted state or a permitted pending proposal.

Snapshots retain the context needed for replay, including the equipment/resources/campaign state at acceptance. Those snapshots do **not** lock current resources: new action rolls may be appended and current HP, spell uses, currency, equipment, and notes continue to change outside selection history.

The ledger detects inconsistent edits, missing records, changed accepted roll evidence, and rule-invalid replay. It is not an authentication mechanism: a self-contained save cannot prove that someone has not coherently rewritten its entire history and marker. Likewise, baseline acquisition levels are explicit player declarations validated for feasibility, not independently verified campaign memories.

## Invocation acquisition and prerequisites

Untracked creation drafts retain their original editable creation behavior. Starting history requires acquisition levels for all existing Warlock invocations. The declared level must be high enough for its slot and prerequisites and no higher than the character's current Warlock level. A prerequisite invocation cannot be declared as acquired after an invocation depending on it.

Later accepted additions and replacements, including changed bindings on repeatable invocation instances, record the actual class and character level of the event. `srd55HistoryAcquisitionLevels` exposes the owning invocation path's character level to feature evaluation. Lessons of the First Ones, including Skills used by later Expertise, can therefore use the recorded acquisition rather than an earliest-possible-level assumption.

## Integration with other lifecycle commands

Resource/rest handlers call `recordSrd55HistoryTrigger` after a valid Short or Long Rest. The hook grants the corresponding replacement window without changing the selections. Wizard-copy transactions call `recordSrd55HistoryChange` with `copy-spell`; copying still owns its required source, payment, time, and slot-level validation. The hooks are no-ops for untracked drafts.

The module's history replay is guarded against recursive evaluation; ordinary public evaluation still validates history and returns actions. No tool calls, file reads, random rolls, clock reads, or automatic conversions occur during history evaluation or replay.

## Validation evidence

The initial history suite passed **229 assertions across 14 test cases** on the development Mac. It uses the public `executeCommand` contract with complete SRD fixtures. Cases cover frozen baselines, published rest/level cadence and shared limits, pending advancement and cancellation, accepted dice preservation, invocation prerequisites and actual acquisition timing, automatic conversion of normal preparations into always-prepared subclass spells, replay consistency, and valid versus tampered spell copying. The command is `build-dev/dnd_tests '[history]'`; later regressions may increase the count.
