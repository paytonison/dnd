# SRD 5.2.1 implementation ledger

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

The experimental `srd55` module implements single-class Fighter and Wizard creation and advancement through level 3. It uses the same `CharacterDocument`, `ResolvedRuleset`, `Evaluation`, choice fields, explanation model, and pack loader as B/X. It deliberately stops at level 3; multiclassing and further advancement require additional implementation.

The reference is the English **System Reference Document 5.2.1**, published May 1, 2025, linked by the [official SRD resource page](https://www.dndbeyond.com/srd). The [official PDF](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf) was inspected on September 8, 2026. It has 364 pages; printed page numbers equal one-based PDF page numbers. No book scans are distributed. The exact attribution and modification statement is in `data/packs/srd55-core/ATTRIBUTION.md` and embedded in the pack manifest. SRD data is CC BY 4.0; application code retains BSD-3-Clause.

| Area | Original pages | Implemented and checked |
|---|---|---|
| Character creation | 19–23 | Class, background, species, nine alignments, Common plus two Standard Languages, standard array, 27-point buy, accepted 4d6-drop-lowest results, background boosts, derived modifiers |
| Advancement | 23 | Level thresholds 0/300/900 XP; optional XP input for milestone campaigns; maximum first-level HP, fixed or accepted HP rolls thereafter; Constitution and dwarf HP modifiers |
| Fighter | 47–49 | d10, skills and saves, training, starting kits, four Fighting Styles, three weapon masteries, two Second Wind uses, Action Surge/Tactical Mind at 2, Champion at 3 |
| Wizard | 77–82 | d6, skills and saves, starting kits; 3 cantrips; spellbook 6/8/10 normal spells; preparations 4/5/6; spell slots 2/0, 3/0, 4/2; Ritual Adept; Arcane Recovery; Scholar expertise; Evoker at 3 with two additional Evocation spells and Potent Cantrip |
| Backgrounds | 83 | Acolyte, Criminal, Sage, Soldier; permitted boosts, fixed skills, tool selection, equipment, and granted Origin feats |
| Species | 84–86 | All nine species; size choices, movement, darkvision, dwarf HP, ancestry/lineage choices, human skill/feat, elf skill, innate spells and casting ability, relevant level-3 spells; nonnumeric traits shown on sheet |
| Feats | 87–88 | Alert, Savage Attacker, Skilled, Magic Initiate (Cleric, Druid, Wizard); Archery, Defense, Great Weapon Fighting, Two-Weapon Fighting |
| Spells | 38, 44, 74, 79–80 | All 15 Wizard cantrips, 30 first-level and 35 second-level Wizard spells; Cleric/Druid cantrips and first-level lists for Magic Initiate; innate spells required by species; levels, schools, concentration/ritual/material flags |
| Equipment | 89–100 | 38 weapons, all 12 armor suits and Shield, tools and starting-kit gear; ownership, purchases and quantities, starting-money balance, attack/damage modifiers, ascending AC, heavy-weapon ability threshold, armor Strength speed penalty, armor/shield training consequences |

The Fighter and Wizard progression tables, background table, weapon table, armor table, and relevant origin tables were inspected as rendered pages, with text extraction used to locate and transcribe entries. Source identifiers and printed-page references travel with data entries and calculations. Tests use independent expected HP, AC, saving throw, skill, slot, and attack values.

## Interpretation and scope boundaries

- Evaluation accepts incomplete drafts, identifies missing/invalid selections, and never rolls dice or reads files. Defaulted values used for previews remain provisional whenever validation errors are present. The application accepts and saves scores and HP results explicitly. `/hp/2` and `/hp/3` hold die results before adding Constitution; first-level HP uses the maximum die automatically.
- Background scores use either +2/+1 on two eligible abilities or +1/+1/+1. Base scores and boosts are stored separately. Species provide no ability-score increases in this ruleset.
- The Wizard's normal spellbook additions are recorded by the level at which each was learned, preventing a second-level spell from being assigned to a level-2 Wizard. At level 3, Evocation Savant grants **two** additional first- or second-level Evocation spells. The ongoing new-slot-level clause applies to subsequent slot-level advancement; the initial level-3 grant is not double counted.
- Origin spells remain separate from Wizard spellbook/preparation choices and use their selected spellcasting ability. A spell learned through Magic Initiate does not automatically enter the Wizard spellbook. Full spell-effect automation is outside this character builder: the sheet lists spells and the source provides casting/effect rules.
- Class skill choices cannot duplicate fixed background skills; Scholar requires an existing proficiency. Human/elf and Skilled proficiency options also show already-known choices as unavailable.
- Wearing untrained armor is possible under the SRD. The sheet warns of Strength/Dexterity D20-test disadvantage and blocked spellcasting. An untrained Shield grants no AC bonus. A two-handed primary weapon with a Shield is marked invalid; mounted-lance handling is outside this experimental slice.
- The initial Wizard spellbook is granted by the Spellcasting feature (page 78), including for a gold-start character. The SRD equipment table does not list a separate blank-spellbook price, so the pack does not invent one or expose a priced replacement-book purchase. Arcane Focus uses the 5 GP staff form from page 96.
- Starting kit contents and starting coin balances are derived from explicit kit selections. Purchases have explicit quantities and cannot exceed combined starting money. Current HP, current coin changes during adventuring, spent slots, and other current resources belong in the document's separate resources object, not in immutable starting HP results.
- Spellbook copying during play, encounter resolution, ongoing spell buffs such as Mage Armor, conditions, magic items, additional classes/subclasses, multiclassing, and levels 4+ are not automated in this experimental slice. The planned full SRD lifecycle remains future coverage. Explicit DM overrides can annotate supported statistics, without claiming missing rules were implemented.

## Acceptance examples

The complete fixtures are in `tests/test_srd55.cpp` and exercise the public content/evaluation contract.

- Dwarf Fighter/Soldier: base Str 15, Dex 14, Con 13, Int 8, Wis 10, Cha 12; background Str +2 and Con +1. Chain Mail + Defense yields AC **17**. Greatsword attack **+5**. Strength save **+5**, Constitution save **+4**. Maximum/fixed HP is **13 / 22 / 31** at levels 1/2/3. Champion critical range starts at **19** at level 3.
- Dwarf Wizard/Sage: base Str 8, Dex 12, Con 13, Int 15, Wis 14, Cha 10; background Int +2 and Con +1. Unarmored AC **11**, spell attack **+5**, spell DC **13**. Maximum/fixed HP **9 / 16 / 23**. Arcana is **+5** at level 1 and **+7** after Scholar expertise at level 2. At level 3, spell slots are **four first-level and two second-level**.
- Fighter accepted HP dice **1** and **10** produce maximum HP **30** at level 3 in that same dwarf fixture. Repeated evaluation and serialization/reload yield byte-equivalent evaluation JSON; there are no random calls in the rules module.
