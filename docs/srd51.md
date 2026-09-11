# 5E (2014): Human Fighter guide

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

Choose **5E (2014) / SRD 5.1 - Human Fighter 1-3 (experimental)** in **File → New**. This creates a character in the original 2014 rules, separately from **5.5E**, the revised 2024 rules. The first implementation covers ordinary Human, Fighter levels 1-3, the Acolyte background, and Champion at level 3. Level 3 is this implementation's current boundary, not Fighter's published maximum.

The character pins edition `srd51`, module `1.0.0`, and pack `srd51-core@1.0.0`. Existing `srd55` versions and saves remain 5.5E. Opening or saving a character does not convert its edition, replace an exact pack version, or roll new dice. The source profile follows SRD 5.1 with original 2014-rule creation and HP/rest corrections separately sourced from the 2015 and 2018 Basic Rules. See [the source ledger](sources-srd51.md).

## Create and equip

1. Select Fighter, Human and Acolyte, then level, optional recorded XP, and alignment. Choose Champion when creating a level-3 draft. XP, when present, must meet the selected level's threshold: 0, 300 or 900.
2. Assign the standard array **15, 14, 13, 12, 10, 8**, use accepted **4d6, drop lowest** results, or enable the DM's **27-point buy** option in Advanced mode. Enter scores before Human's +1 to each ability. Accepted rolls are saved inputs; evaluation never generates them.
3. Choose two Fighter skills, three additional standard languages, and one of the six Fighting Styles. Acolyte already supplies Insight and Religion; if a class selection duplicates one, choose a replacement skill. Common is supplied by Human.
4. Select the four Fighter equipment groups: chain mail or leather/bow/arrows; a martial weapon and shield or two martial weapons; light crossbow/bolts or two handaxes; Dungeoneer's or Explorer's pack. Choose the Acolyte prayer book or prayer wheel. Its other starting items and **15 gp** are granted automatically.
5. Use the optional purchase list to spend those initial coins, then choose worn armor, shield, primary weapon and any offhand weapon from owned items. Purchases show bundle quantities and prices; arrows/bolts come in twenties, needles in fifties, and sling bullets in twenties for **4 cp**. The interface supports one purchase of each item or bundle. Starting inventory quantities are displayed on the sheet.

The pack contains all 12 ordinary armor suits plus shield and 35 ordinary weapons; **Lance and Net remain unsupported**. Validation checks ownership, available hands, compatible two-weapon use and ammunition availability. Armor computes its Dexterity contribution, shield and Defense bonuses, Stealth disadvantage and any heavy-armor Strength speed penalty. Finesse weapons allow the applicable Strength/Dexterity choice; versatile weapons expose two-handed use. Click the corresponding sheet statistic to inspect its arithmetic and sources.

All six Fighting Styles are available. Archery, Defense, Dueling and Two-Weapon Fighting contribute to applicable calculations. Great Weapon Fighting and Protection provide rule guidance on the sheet; their dice rerolls, reactions and encounter timing are resolved at the table. Equipment-pack contents are descriptive text, not a container or consumption model.

## Advance and track resources

Once the character is complete, use **Character → Actions…** to preview and accept a persistent action. Actions keep current HP and spent resources separate from the scores and Hit Dice used to create the character. They are unavailable when the document or its exact ruleset requires correction.

| Action | Inputs and resulting behavior |
| --- | --- |
| Advance one Fighter level, fixed HP | Record at least 300 XP before level 2 or 900 before level 3. Adds fixed 6 + Constitution modifier, minimum 1 HP. Select Champion for level 3. |
| Advance one Fighter level, rolled HP | Same XP and subclass requirements; accept a d10 result from 1 to 10. Adds that result + Constitution modifier, minimum 1 HP. |
| Use Second Wind | Accept a d10 result, spend one use and heal the result + Fighter level, capped at maximum HP. Stock Fighter has one use at every supported level. |
| Use Action Surge | Spend one available use; resolve the extra action at the table. Capacity is zero at level 1 and one at levels 2-3. |
| Finish a short rest | Confirm a qualifying rest of at least one hour. Restore Second Wind/Action Surge; optionally spend available Hit Dice, each healing its accepted d10 + Constitution modifier, minimum zero. A displayed die input of zero means it is not spent. |
| Finish a long rest | Confirm the published rest requirements. Restore HP and class uses; recover half total Hit Dice rounded down, minimum one, capped at total capacity. This restores one spent die at each supported level, not necessarily every spent die. |

For a long rest, confirmation includes at least eight hours total with six hours of sleep, no more than two hours of light activity, at least 1 HP at its start, no prior long-rest benefit within 24 hours, and the published interruption requirements. The application records the acceptance; the table determines whether those conditions occurred.

Accepted advancement preserves existing wounds, adds one Hit Die, retains spent class uses, and grants newly acquired capacity. For example, advancing from 12 maximum/7 current HP with an 8-HP gain produces 20 maximum/15 current HP, preserving five missing HP. It records the selected class, level, HP base and fixed/rolled method in history. Those accepted level/class/HP selections, and the level-3 subclass, become read-only in the ordinary editor. Changing the future HP method does not reinterpret an accepted result.

Current `hp`, `hitDice`, `secondWind` and `actionSurge` belong to the document's resources object. Changing current HP must not change accepted advancement Hit Dice or maximum-HP inputs. Invalid resource amounts and conflicting action/level history produce diagnostics. Preview and failed actions leave the original document unchanged; applying a valid action records history and uses the shared atomic save workflow when saved.

## Options, sources and saves

Point-buy permission inherits per key from campaign defaults. An absent character `allowPointBuy` inherits the campaign value; explicit `false` overrides it. An unrelated character option does not suppress inheritance. Advanced mode changes visibility while preserving saved choices and effects.

Unknown character options stored as boolean `false` are preserved without an effect. Unknown active or malformed options block completion. Unsupported character fields, malformed accepted values and unavailable selected records remain visible as diagnostics rather than being converted into valid-looking defaults. Re-enable the required exact source version to restore a disabled selection's meaning.

For developers and hand-authored files, saved choice objects use named keys: `startingWeapons.weapon1` and `startingWeapons.weapon2`, plus `hp.level2` and `hp.level3`. Public field paths are `/startingWeapons/weapon1` and `/hp/level2`, for example. These are object members, not numeric JSON-pointer components that could create arrays. Current-resource keys remain separate. `tests/srd51_fixture.hpp` gives a complete, independently chosen baseline input; it is not a substitute for validating an imported character.

The sheet, HTML and PDF exports use the shared evaluation results and retain edition, source and override annotations. A successful export alone does not establish visual acceptance; [the September 11 validation checkpoint](5e-validation-20260911.md) records which automated, native, export and packaging checks were actually performed.

## CLI

Use the repository's documented build commands first. These commands create and inspect a new draft; a blank draft returns evaluation status **2** until its choices are completed in the GUI or JSON file.

```sh
./build/dnd-cli editions
./build/dnd-cli validate-pack data/packs/srd51-core
./build/dnd-cli new srd51 fighter-5e.dnd.json 1.0.0
./build/dnd-cli evaluate fighter-5e.dnd.json
./build/dnd-cli sheet fighter-5e.dnd.json fighter-5e.html
```

Actions read a JSON input file. For a complete character with an available Second Wind use, create `second-wind.json` containing:

```json
{ "roll": 4 }
```

Preview its changes and then apply to a separate destination:

```sh
./build/dnd-cli preview fighter-5e.dnd.json srd51.second-wind second-wind.json
./build/dnd-cli apply fighter-5e.dnd.json srd51.second-wind second-wind.json fighter-5e-after-wind.dnd.json
```

`preview` reports choice/resource changes and the proposed character without saving it. `apply` writes its output only for a valid result. Other action inputs are:

| Action ID | JSON input example |
| --- | --- |
| `srd51.advance-fixed` | `{}` when advancing to level 2; `{ "subclassId": "srd51:champion" }` when advancing to level 3 |
| `srd51.advance-rolled` | `{ "hpRoll": 7 }`; add `"subclassId": "srd51:champion"` when advancing to level 3 |
| `srd51.action-surge` | `{}` |
| `srd51.short-rest` | `{ "eligible": true, "die1": 6 }` when at least one die is available; omit die fields to spend none |
| `srd51.long-rest` | `{ "eligible": true }` |

The evaluator's `actions` output lists currently available actions and input fields. The CLI accepts an optional final pack-directory argument for `evaluate`, `sheet`, `preview` and `apply`. Exact edition/module/pack resolution applies to all four; a missing version is not silently substituted. Exit status is **0** for a valid complete result, **2** for an invalid/incomplete result, and **1** for a command/file error.

## Remaining scope

This is a starting slice of 5E. Other classes, races and backgrounds; other archetypes; levels 4-20; feats/variant Human; spellcasting; multiclassing; exotic languages; starting-gold alternatives; magic items; Lance/Net; general acquisitions, sales and consumption; containers; encumbrance; and combat adjudication remain unimplemented. There is no full SRD 5.1 or Player's Handbook coverage claim. [The coverage ledger](coverage.md) and [the plan](../PLAN.md) retain the broader edition goals and all earlier outstanding release gates.
