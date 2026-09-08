# SRD 5.2.1 lifecycle transactions

The expanded `srd55` module, version `2.0.0`, exposes explicit actions through the same library, command-line tool, and desktop interface. A command takes a character, its exact resolved rules, an action ID, and structured inputs. It returns either a complete proposed document or the original document with explanations. Evaluation itself never spends, rolls, rests, copies, or advances a character.

The GUI's **Character → Actions…** menu displays the currently available actions and their source references. Preview shows changes without modifying the live character. Apply accepts only the exact previewed candidate, provided the live document has not changed. Action inputs are validated again by the core against the currently announced fields and choices. Unknown inputs, stale options, missing sources, invalid costs, and failed rule validation cannot partially alter the character.

Successful commands append a journal entry containing the exact inputs, module version, changed choices and resources, and references. Accepted action rolls are retained separately from creation and advancement rolls. A failed game check can still be a valid transaction: failing to copy a scroll consumes its materials and records the failed check. An invalid request returns the original document.

## Rests and current resources

| Action | Implemented behavior | Source |
|---|---|---|
| Spend a resource | Select a currently defined resource and spend a positive amount no greater than its current value. | Owning resource's feature |
| Short Rest | At least one uninterrupted hour, starting with at least 1 HP; applies each class's full or partial recovery and opens the rest's replacement/Hit Die window. | [187](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=187) |
| Spend a Hit Die | Record one accepted die result from an available pool. Add current Constitution, with minimum 1 HP healing, up to the effective maximum. Spend the die without changing accepted level-up HP rolls. | [187](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=187) |
| Long Rest | Normally 8 hours including 6 asleep; Elf Trance requires 4 hours. Resuming interruptions adds one hour each. A later Long Rest requires the stated 16-hour interval. Restores HP, Hit Dice, and applicable resources; advances the actual Divine Intervention cooldown and rest-based replacement permissions. | [185](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=185), [84](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=84) |
| Unfinished Long Rest | A qualifying hour grants Short Rest benefits, including the Ranger's Tireless exhaustion reduction. It does not restore HP or grant Long Rest replacement permissions. | [185](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=185), [59](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=59) |
| Arcane Recovery | At the recorded Short Rest, recover expended level 1–5 slots within the rounded-up half-Wizard-level budget, spending the feature's once-per-Long-Rest use. Pact slots normally already recovered fully at this rest and cannot be restored above capacity. | [78](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=78) |
| Sorcerous Restoration | At the recorded Short Rest, regain up to rounded-down half Sorcerer level in spent Sorcery Points, once per Long Rest. | [66](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=66) |
| Magical Cunning | Record the one-minute rite, expend its use, and restore the published number of Pact Magic slots; Eldritch Master restores all. | [72–73](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=72) |
| Font of Magic | Convert a slot to points or spend the source-specific points to create a slot at a legal Sorcerer level. Created slots expire on a Long Rest and cannot unlock higher-level class preparations. | [65](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=65) |
| Font of Inspiration | Expend a Spellcasting or Pact Magic slot to recover one spent Bardic Inspiration use. | [32](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=32) |

Resource maxima are linked to their displayed calculations, including applicable reasoned overrides. The normal and effective statistics remain inspectable. A maximum-HP reduction is a separate current input; the general Long Rest restores that maximum. Specific effects that expressly prevent ordinary recovery still require their own implementation or an explicit campaign adjustment; this is not a universal condition or spell-effect simulator.

Current-resource edits remain deliberate user inputs, separate from recorded transactions. The application records declared elapsed time and accepted events; it does not infer that real-world time passing means the character rested.

## Wizard copying

`srd55.spells.copy` copies from another spellbook. It requires a level 1+ Wizard spell of a level the Wizard can prepare, an identified source, at least two hours and 50 GP per spell level, and sufficient current funds. The money and learned-spell record change atomically. Normal level gains and Savant grants remain distinct from copied spells. See [SRD 78](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=78).

`srd55.spells.copy-scroll` selects an actual owned, carried Spell Scroll instance. Its spell and level come from that instance. The command uses the accepted d20 plus the effective Intelligence (Arcana) modifier; Reliable Talent applies to a proficient check when available. An additional resolved bonus requires an explanation. The DC is 10 plus spell level. A natural 1 is not automatically a failed ability check.

Success adds the spell, records the copying provenance and check, spends the copying funds, and consumes the scroll. Failure spends the same funds and consumes the scroll but adds no spell. Both outcomes retain accepted check evidence and preserve the inventory instance's provenance. An incomplete duration, insufficient funds, invalid source, unavailable spell, malformed input, or duplicate known spell rejects the whole request before committing any change. See [SRD 244](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=244).

Owned inventory initialization, acquisitions, attunement, item effects, and item coverage are documented separately in `srd55-inventory.md`. Accepted acquisition and replacement history is documented in `srd55-history.md`.

## Command-line use

```sh
dnd-cli preview CHARACTER ACTION_ID INPUTS_JSON_FILE [PACK_DIRECTORY]
dnd-cli apply CHARACTER ACTION_ID INPUTS_JSON_FILE OUTPUT_FILE [PACK_DIRECTORY]
```

Preview prints the proposed character and changes without saving. Apply atomically saves a valid result. A rejected same-path Apply preserves the original bytes. Input JSON is bounded to 16 MiB, and missing exact module/pack versions remain inspection-only.

`output/action-qa/cli-actions.json` records a real CLI check: preview preserved the source, Apply saved exactly the previewed document, and an invalid subsequent same-path Apply preserved that file. Native ActionDialog evidence and the complete GUI test scope are in `gui-versioning.md`.

## Scope and validation

The focused tests exercise deterministic repetition, rollback, accepted-roll preservation, partial and complete rests, resource caps, source-specific recovery limits, temporary-slot expiry, Wish/Tireless regressions, and successful and failed scroll copying with an accepted history ledger. The combined validation log is recorded in `v2-progress.md` after each stable integration milestone.

The larger plan remains active. Explicit transactions do not establish complete SRD publication coverage, complete magic-item effect automation, or support for the other planned editions and supplements.
