# SRD class resource actions

The expanded SRD module exposes these transactions through the generic **Character → Actions…** dialog and the public `executeCommand` API. Preview is deterministic: it creates a candidate document, applies no random rolls, and leaves the open character unchanged. Apply commits the reviewed candidate only while the source document still matches the preview. A failed command returns the original document, including its resources, campaign settings, and accepted roll evidence.

The rules below were checked against the official [SRD 5.2.1](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf). Page references use the document's printed page numbers.

| Action suffix under `srd55.classes.` | Behavior | SRD page |
| --- | --- | --- |
| `druid.slot-to-wild-shape` | At Druid 5+, spend any available ordinary or Pact Magic slot to recover one Wild Shape use, requiring zero remaining Wild Shape uses and an explicitly identified own turn. | 43 |
| `druid.wild-shape-to-slot` | Spend one Wild Shape use and the separate Wild Resurgence daily permission to produce one level 1 slot under the recorded campaign ruling. | 43 |
| `druid.nature-magician` | At Druid 20, spend one to four Wild Shape uses to produce a single slot of twice that level, spending Nature Magician's separate daily permission. | 43 |
| `druid.natural-recovery` | After a Short Rest, recover expended level 1–5 slots whose combined levels do not exceed half Druid level rounded up; consume the spell-slot recovery permission without consuming the separate free Circle spell permission. | 46 |
| `initiative` | Record an accepted Initiative event and apply eligible automatic minimum recoveries, with explicit opt-ins for Persistent Rage and Uncanny Metabolism. | 30, 33, 43, 51–52 |
| `restore-intimidating-presence` | Spend one Rage use to restore one expended Intimidating Presence use. | 30 |
| `restore-holy-nimbus` | Spend one level 5 spell slot to restore one expended Holy Nimbus use. | 57 |
| `restore-dragon-wings` | Spend three Sorcery Points to restore one expended Dragon Wings use. | 70 |
| `restore-hurl-through-hell` | Spend one Pact Magic slot to restore one expended Hurl Through Hell use. | 76 |
| `activate-innate-sorcery-with-points` | At Sorcerer 7+, with no Innate Sorcery uses remaining, spend two Sorcery Points and explicitly confirm the Bonus Action to activate the existing Innate Sorcery effect flag. | 66 |

Restoring a feature's permission does not activate its effect. The final action above specifically performs activation and therefore changes the existing effect flag. Other combat effects, target resolution, and attack outcomes remain explicit table decisions.

## Druid-created slot ruling

The Druid text does not specify the expiry of slots produced by Wild Resurgence or Nature Magician. The app does not silently borrow Font of Magic's expiration rule. Each reverse-conversion form requires a campaign ruling and a nonblank reason. There is no default on first use; later forms visibly prefill the saved ruling.

| Ruling | Saved treatment |
| --- | --- |
| `recover-expended` | Recover one expended slot within the existing capacity. Reject the conversion if that level has no expended slot. |
| `additional-until-long-rest` | Add one current slot and one extra capacity in `resources.druidCreatedSpellSlots[level]`; the completed Long Rest command removes those extras. |
| `additional-until-spent` | Add one current slot and one extra capacity in `resources.druidPersistentSpellSlots[level]`; ordinary slot-spending commands consume these extras first and reduce their capacity. An unspent extra survives a Long Rest. |

The selected `{policy, reason}` is saved at `campaign.srd55DruidSlotRuling` and printed on the sheet as a campaign ruling. Changing it during a later conversion applies to new slots; previously recorded extras retain their original storage and treatment. Font of Magic's `createdSpellSlots` remain separate. Creation of a higher-level slot never adds preparation permissions for higher-level class spells.

## Accepted events and resource limits

Initiative takes a distinct `eventId`, an accepted d20 result, and confirmation that Initiative was rolled. It restores Bardic Inspiration to a minimum of two at Bard 18, Wild Shape to a minimum of one at Druid 20, and Focus Points to a minimum of four at Monk 15 when Uncanny Metabolism is not used. These floors never reduce a pool that already exceeds them, and effective capacity overrides remain respected.

Persistent Rage requires an explicit opt-in and spends its once-per-Long-Rest permission while recovering expended Rage uses. Uncanny Metabolism requires an explicit opt-in plus the accepted Martial Arts die result; it restores Focus and heals Monk level plus that die result, capped by effective maximum HP. The accepted Initiative and healing results are retained in `rolls.classActions[eventId]`. A repeated event identifier is rejected, including identifiers with existing accepted roll evidence. Editing or evaluating a character does not trigger these recoveries.

Wild Resurgence's forward conversion takes a distinct `turnId` and confirmation of the character's own turn. The command journal prevents repeating that conversion for the same identified turn. The app does not infer encounter or turn boundaries from elapsed time. The table supplies the event identifiers and accepted rolls.

The core validates each command against the currently announced fields and options before dispatch. The module then checks class permissions, current balances, recovery limits, and prerequisites. Natural Recovery retains its qualifying Short Rest window; the other class actions close that window. The core records successful commands in the advancement journal, and the GUI's normal autosave and atomic save behavior applies.

## Verification

`tests/test_srd55_class_actions.cpp` uses complete public-evaluation fixtures and exercises costs, rollback, repeated events, accepted dice, separate daily permissions, all three Druid slot policies, and consumption through both generic resource spending and Find Familiar casting. It also checks that preparation/creation choices and their roll evidence are preserved. These tests supplement the generic Actions dialog's existing malformed-input and stale-preview coverage.

The focused suite passed **302 assertions across 11 cases** on September 8, 2026, using the separate `build-class-actions` build with the current shared rules sources. The Uncanny Metabolism case also exercises an accepted character-history baseline and JSON save/reopen roundtrip. The new files pass the repository whitespace check and use its `.clang-format` style. No native UI, PDF, or package regeneration was part of this class-action change.
