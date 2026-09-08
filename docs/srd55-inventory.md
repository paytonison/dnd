# SRD 5.2.1 owned inventory and magic items

This module adds an explicit owned-item lifecycle to SRD module `2.0.0`. Creation kits and purchases remain character-creation choices until the user runs **Begin owned inventory**. The accepted physical items and remaining money are then frozen into an instance ledger. Recalculation displays the ledger and derives eligible item effects; it never purchases items, accepts rolls, restores charges, or completes rests.

The packaged catalog contains **all 258 named magic-item headings** from SRD pages 209–253. Its current coverage labels are **45 implemented, 14 partial, and 199 cataloged**. These counts refer to parent item records; templates such as Ioun Stone contain more specific variant coverage. There are 140 attunement-required records and 48 records with charge, daily-use, or consumable counters. Catalog inclusion is not proof of executable mechanics. The application remains experimental, and this increment does not complete every magic-item rule in the plan.

## Sources and license

The source is the official [System Reference Document 5.2.1 PDF](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf), 364 pages, SHA-256 `8974902d109d6e63672d7c490bde9ccf052410503d9cfa768237154fbc5e3d87`. Printed page numbers and PDF page numbers agree. The pack contains the required attribution:

> This work includes material from the System Reference Document 5.2.1 ("SRD 5.2.1") by Wizards of the Coast LLC, available at https://www.dndbeyond.com/srd. The SRD 5.2.1 is licensed under the Creative Commons Attribution 4.0 International License, available at https://creativecommons.org/licenses/by/4.0/legalcode.

Original descriptions are converted to structured records with normalized typography and line wrapping. Progression/variant tables and typed effect metadata are adaptations. The code is separately licensed under BSD-3-Clause. The generator verifies the exact source hash, discovers item headings from their PDF typography, and rejects collisions with ordinary content IDs. The generic bonus weapon uses `srd55:magic-weapon-bonus`, because `srd55:magic-weapon` already identifies a spell. The original source PDF and rendered reference pages are not distributed in the repository.

| Rule or records | Official SRD pages | Implemented consequences |
| --- | --- | --- |
| Higher-level starting equipment | [24](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=24) | Explicit GM-approved award, accepted d10, rarity allowances, once per creation ledger |
| Identification, attunement, wearing and wielding | [102–103](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=102) | Ownership, distinct identification/attunement events, prerequisites, equipment and bond endings |
| Activation, charges, curses | [204–208](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=204) | Typed properties, accepted recharge/depletion rolls, persistent item curses |
| Thief: Use Magic Device | [64](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=64) | Four attunements; d6 charge conservation; class prerequisites retained |
| Armor and protection items | [209–216](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=209) | Numeric bonuses, selected base armor, armor conditions, typed resistance and vulnerability |
| Ability items, belts, Berserker Axe | [209, 213, 223, 225](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=213) | Minimum-setting effects, capped current increases, language/training, HP and persistent curse |
| Ioun Stone | [227–228](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=227) | Variant identity, maximum three orbiting, supported passive bonuses; absorption/storage remain limited |
| Periapt of Health | [234](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=234) | Attunement, Poisoned-save advantage trait, once-daily 2d4 + 2 healing |
| Potions of Healing | [236](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=236) | Four rarities, correct dice, self/other recipient, one consumed dose |
| Scroll profiles and copying | [244](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=244) | Owned spell/rank/DC/attack profile, consumption on copying success or failure |
| Supported staff and wand properties | [245–251](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=245) | Charge costs, legal cast levels, fixed or actor casting ability, recharge and depletion |
| Manuals and tomes | [229–230, 249–250](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=249) | 48-hour study, ability increase up to 30 at acquisition level, century of dormancy |

The belt, Ioun, potion, and scroll tables were visually inspected in the original PDF, including the scroll table split across columns on page 244. The generator corrects one extraction-order issue: the Apparatus of the Crab lever table belongs to its own entry, not the following Armor of Resistance entry.

## Public commands

Commands run through `executeCommand`, with module-declared input fields exposed by the desktop action dialog. Invalid input returns the original document. Successful commands preserve an inventory event and the common command journal. UI defaults are visible convenience values; the core does not silently fill omitted API inputs with UI defaults.

All names below have the prefix `srd55.inventory.`.

| Command | Accepted operation |
| --- | --- |
| `initialize` | Freeze `Evaluation.moduleData["inventory.creationSeed"]` and initial money; do not replace an existing canonical wallet |
| `adjust-currency` | Apply signed nonzero `deltaCp` with a nonblank reason; prevent overdrawing funds |
| `acquire` | Record loot, gift, purchase, or approved starting allowance; validate base, variant, quantity, spell binding and total price |
| `dispose` / `recover` | Remove or return the same physical instance; preserve charges and magical state; destroyed items cannot be ordinarily recovered |
| `equip` / `unequip` | Select physical gear, split stacks, resolve exclusive locations and hand conflicts without duplicating an item |
| `move` | Carry or store owned items; storage suppresses current worn/held/on-person effects but preserves bonds and persistent curses |
| `identify` | Record already completed identification by its own Short Rest or Identify spell |
| `attune` / `unattune` | Record a completed focused rest or an already resolved published ending; enforce prerequisites, duplicate-copy restrictions and capacity |
| `release-curse` | Record a resolved Remove Curse or equivalent ruling; clear this owner's curse and bond without permanently rewriting the source item |
| `use` | Execute supported item spell/healing activation, accepted roll, charge cost and optional depletion result |
| `recharge` | Record one accepted recharge event; unique event reference prevents reusing the same event for the same item |
| `study` / `awaken-tome` | Record completed study or a century of elapsed game time; persist the earlier ability event independently of the book |
| `higher-level-guide` | Apply the optional published initial equipment allowance once, based on frozen creation level |

Identification and attunement are separate accepted events. Their booleans represent already completed play; recalculation does not infer a rest or cast Identify. Similarly, recharge event references describe accepted game events, not scheduled real-world timers. The higher-level starting guide requires an explicit approval flag and accepted die, rather than treating the table as an automatic entitlement.

## State and integration contract

`resources.currencyCp` is the current wallet shared by inventory, spell-copy, and other resource commands. Old `resources.gp` remains legacy data. Inventory events record their own balance before/after; the general command journal also captures costs from other modules. Never reconstruct the wallet solely from inventory events or reset it from the creation budget.

```json
{
  "inventory": {
    "initialized": true,
    "nextId": 7,
    "creationLevel": 1,
    "creationCurrencyCp": 1500,
    "instances": [{
      "id": "item-6",
      "itemId": "srd55:magic-wand-of-magic-missiles",
      "quantity": 1,
      "status": "owned",
      "location": "carried",
      "equipped": true,
      "slot": "main-hand",
      "attuned": false,
      "identified": true,
      "curseActive": false,
      "dormant": false,
      "charges": 4
    }],
    "events": [],
    "permanentEffects": []
  }
}
```

Magic acquisitions create separate physical instances even when the user acquires several copies. Ordinary gear may stack; equipping one splits its stack. Statuses are `owned`, `disposed`, `destroyed`, and `consumed`. Historical instances remain available for provenance; a recovered instance retains its old charges. Supplemental `baseProfile`, `variantId`, and scroll fields identify the accepted form of a variable item. Owned creation gear is never regenerated from virtual attack profiles or Pact of the Blade aliases.

`resolveInventory(Context)` returns eligible current armor/weapon/shield profiles, ability minimums, current ability additions/caps, training, languages, numeric bonuses, typed sourced traits, item spell profiles, and permanent events. The ordinary evaluator then recomputes derived values. Bracers of Archery grant the actual bow proficiency before attack calculation. Bracers of Defense require no armor or shield. Magic shields require shield training for their numeric AC contribution. Merged Wild Shape gear does not contribute current worn-item effects.

Current additive ability increases are applied to the natural score subject to their own cap, then minimum-setting items impose their floor. Thus Strength 8 with an Ioun Stone of Strength and Gauntlets of Ogre Power is 19; natural Strength 18 with those items becomes 20. This composition implements the gauntlets' rule that they have no effect when Strength without them is already 19 or higher. Permanent manuals/tomes instead add a sourced timeline event at the accepted character level. They do not rewrite base scores or allow a later ordinary feat to increase a score beyond its own cap.

Item spell profiles preserve `sourceType:"item"`, source item/instance identity, `castLevel`, `chargeCost`, `activationActionId`, and `ability`. Fixed casting statistics are `fixedDC` and `fixedAttack`; `usesActorCastingAbility` distinguishes a staff using the actor's valid casting ability. Item-granted spells do not themselves qualify the owner for a spellcaster attunement prerequisite. For example, Wand of Fireballs retains DC 15 rather than adopting a Wizard's current DC. Staff of Frost spends four charges to cast the level-6 Wall of Ice; charge count and spell level are distinct.

Thief charge conservation applies to actual `charges`, not `uses` or `consumable` units. An accepted d6 of 6 preserves the charged item's cost, but the owner must have enough charges before use. The source's last-charge destruction roll is separately accepted. The Thief gains four attunement slots at Rogue 13, retains class prerequisites, and is still limited to three orbiting Ioun Stones.

## Owned spell scrolls

A typed scroll definition has `kind:"magic-item"`, `spellSelection:true`, ranked variants carrying `scrollLevel`, `saveDc`, and `attackBonus`, and one consumable use. This is a supported mechanics binding, not a prose category check; other namespaces can use the same validated schema. Every acquired scroll stores:

```json
{
  "itemId": "srd55:magic-spell-scroll",
  "variantId": "level-3",
  "spellId": "srd55:fireball",
  "scrollLevel": 3,
  "scrollSaveDc": 15,
  "scrollAttackBonus": 7,
  "quantity": 1,
  "charges": 1
}
```

The spell's level must match the selected published rank. The evaluator validates the saved rank and casting profile against the exact source variant. `consumeSrd55InventoryScroll` consumes one owned, carried, valid scroll, retains its record with quantity/charges zero and `status:"consumed"`, and records why it was consumed. The helper never charges money or updates a spellbook.

The lifecycle module's public `srd55.spells.copy-scroll` command combines the helper with Wizard eligibility, actual cost/time, accepted Intelligence (Arcana) check against DC 10 + spell level, copied-spell history, and accepted-roll history. Both success and failed checks consume the scroll. Failure does not add a spell. Invalid input leaves inventory, currency, and choices untouched. Scroll casting and its higher-level casting check remain unimplemented source-reference mechanics in this increment.

## Coverage boundaries

An `implemented` record means its declared character-sheet effects or exposed property actions are supported at this scope. Typed resistance, immunity, conditional advantage, and movement traits are displayed with sources; this application does not resolve an encounter's targets, damage, movement, or reaction timing merely by listing them. Examples such as Cloak of Displacement, Staff of Power, Staff of the Magi, optional Staff of Striking damage, spell storage/absorption Ioun Stones, and magic ammunition are explicitly partial. A `cataloged` record retains its description and ownership but does not silently contribute unimplemented magic. Equipping an item with unknown passive mechanics produces a blocking coverage message; carrying it produces a warning.

The remaining work includes the 199 reference-only parent records and the specific `coverage.remaining` details on partial records; scroll casting; timed activation/suppression states; spell storage and absorption; sentience and artifact interactions; container capacity and automatic encumbrance; and encounter actions such as forced berserk attacks. Inventory possession, notes, or generic charge metadata alone must not be described as full implementation of these rules.

## Rebuilding and validation

The version-2 manifest includes both `content.json` and `magic-items.json`; regenerating the base pack retains both names. Run the base generator first when rebuilding both artifacts, then the item generator with the same official PDF:

```sh
python3 scripts/build-srd55-v2-data.py --pdf /path/to/SRD_CC_v5.2.1.pdf
python3 scripts/build-srd55-magic-items.py --pdf /path/to/SRD_CC_v5.2.1.pdf
cmake -S . -B build-inventory -DDND_BUILD_GUI=OFF -DDND_BUILD_TESTS=ON
cmake --build build-inventory --target dnd_tests -j4
./build-inventory/dnd_tests '[inventory]' --reporter compact
```

Both generators require the source text/PDF dependencies described in their scripts; the item generator uses PyMuPDF and PyPDF. An optional `--cache` avoids repeated text extraction while retaining the source-hash check. Generated JSON is deterministic.

Inventory regression cases use the public evaluator and command API with complete characters. They cover exact creation quantities and wallet preservation; atomic purchase/sale/recovery; attunement requirements and stacking; four Thief attunements versus three orbiting stones; current versus permanent ability effects; persistent curses; charge costs, accepted rolls, depletion and repeated recharge prevention; higher-level allowances; explicit currency; stored gear and bow training; fixed spell DCs; natural-score chronology; inactive Magic Initiate choices; potion consumption; scroll profiles/consumption and supplementary namespaces; malformed executable import data. The lifecycle suite separately tests complete successful and failed Wizard scroll-copy transactions.

Latest isolated inventory validation: **819 assertions across 20 test cases passed** after formatting. Rebuilding `magic-items.json` from the verified official PDF produced a byte-identical file. These results establish the stated inventory cases; they do not upgrade catalog-only entries to implemented mechanics. The root task performs the final combined headless and desktop builds after the shared sources are stable.
