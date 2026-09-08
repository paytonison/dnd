# SRD 5.2.1 familiars and companion state

The companion module implements **Find Familiar casting and persistent state** through the shared `CharacterCommand` interface. It consumes resolved spellcasting profiles and the verified current Wizard spellbook from `Evaluation.moduleData`; it does not search raw future spellbook entries or infer a missing casting permission.

The primary references are [Find Familiar, page130](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=130), [general ritual casting, page104](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=104), [Wizard Ritual Adept, page78](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=78), [Pact of the Chain and its Investment, pages73–74](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=73), and [Druid Wild Companion, page43](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=43).

## Casting routes

Each actual casting permission produces its own action. The action uses the source's casting ability and resource identifier, so the same spell granted by a class and by Magic Initiate does not acquire a shared or invented free-use pool.

| Permission | Required completed casting | Resource cost | Incense |
|---|---|---|---|
| Wizard Ritual Adept |70 minutes; spell must be in the resolved Wizard spellbook, but need not be prepared | No slot |1000 cp |
| Prepared-spell ritual, including Magic Initiate or a prepared Pact of the Tome ritual |70 minutes | No slot |1000 cp |
| Prepared spell using a slot |60 minutes | One selected eligible Spellcasting or Pact Magic slot |1000 cp |
| A source-linked free use, such as Magic Initiate |60 minutes | One use from that exact grant's resource |1000 cp |
| Pact of the Chain |Magic action | No spell slot |1000 cp |
| Wild Companion using Wild Shape |Magic action | One Wild Shape use |No material cost |
| Wild Companion using a spell slot |Magic action | One eligible Spellcasting or Pact Magic slot |No material cost |

The paid routes atomically purchase and consume the required10 GP of incense from the canonical `resources.currencyCp` balance. If owned inventory has not been initialized, the existing calculated remaining creation balance supplies the starting current balance. Druid routes neither require nor create a wallet.

A casting requires an affirmative completed input and sufficient accepted casting time. Interrupted or incomplete castings, unavailable sources, ineligible forms, insufficient funds, and insufficient uses/slots return the original document. The casting command does not wait for real time or roll dice. Accepted casting inputs are saved as evidence, and reevaluation does not charge again.

The module also respects current casting restrictions: unconscious/incapacitated state,0 HP, active Rage, untrained armor, and the applicable Wild Shape casting restriction. An untrained Shield is treated according to its separate SRD rule rather than importing an older edition's armor penalty.

## One familiar across all sources

There is at most **one Find Familiar familiar**, including a temporarily dismissed familiar, regardless of how many sources grant the spell. The compatible storage remains `resources.familiars/<owner>`; ordinary Wizard casts use `wizard`, Pact-related casts use `warlock`, Wild Companion uses `druid`, and other spell grants use `spell`.

A new valid casting clears every prior owner-form reference before recording the selected form under the new owner. It retains the familiar's identity across a change of source or form, and retains that identity when a vanished familiar is called again. Legacy documents with multiple owner-form records receive an explicit validation error; a valid new casting can repair them by establishing a single recorded familiar.

Normal forms are the available SRD Beast stat blocks with CR0. A character with Pact of the Chain can also use its published special forms. A Wild Companion casting is always **Fey** and expires when its owner completes a Long Rest, even if the character also has Pact of the Chain. Ordinary and Pact-source familiars persist through their owner's rests.

The dedicated familiar sheet includes its own form, creature type, HP, status, speeds, source, and class-feature modifications. It has no normal Attack permission. Pact of the Chain exposes the reaction attack obtained by forgoing an owner's attack. Investment of the Chain Master supplies the chosen40-foot movement, the Warlock's save DC, the Bonus Action attack command, permitted damage-type substitutions, and the reaction Resistance rule. The dependent save DC refreshes after common character-stat overrides while preserving the normal value; a direct override of the companion profile remains explicit.

## State commands

- `srd55.companions.dismiss` uses a Magic action to send the familiar to its pocket dimension and preserves its HP.
- `srd55.companions.return` returns that living, temporarily dismissed familiar to a space within30 feet. It does not heal it or require another casting.
- `srd55.companions.dismiss-permanently` ends its service.
- `srd55.companions.damage` records already resolved damage; at0 HP the familiar vanishes and another casting is required.
- `srd55.companions.heal` records already resolved healing to an active living familiar, capped at its form's maximum HP.

Current form/type controls from older UI stages are marked read-only so a free field edit cannot bypass a casting cost or the one-familiar rule. State commands and completed castings provide the supported mutations. This is companion bookkeeping, not an automated combat referee.

## Rest and acquisition-history integration

`expireSrd55CompanionsOnLongRest` is called by the completed Long Rest handler. It explicitly clears Wild Companion's active form while preserving the expired record and identity.

Every completed Find Familiar casting closes the prior resource-rest window and calls `recordSrd55HistoryTrigger` with `cast-find-familiar`. For tracked characters, this opens the published Investment movement-choice opportunity without granting an unrelated rest or level replacement. Casting during pending acquisition-history work is rejected atomically.

The public contracts are declared in `include/dnd/companions.hpp`; source and state commands use the same evaluation/action/transition contracts as the rest of the application. Spell Scroll and other magic-item casting permissions remain owned by the inventory module and are not silently converted into a free companion route.
