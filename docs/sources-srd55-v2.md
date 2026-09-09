# SRD 5.2.1 version-2 data ledger

`data/packs/srd55-core-v2` is a separate pack with ID **srd55-core**, version **2.0.0**, edition **srd55**, and supported module version **2.0.0**. The legacy version-1 pack and its tests remain independent. This ledger records data coverage and verification; it does not claim that a cataloged description is an executed mechanic. The module and feature acceptance suites establish execution coverage separately.

The source is the [official English SRD 5.2.1 PDF](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf), linked from the [official SRD resource page](https://www.dndbeyond.com/srd), inspected September 8, 2026. It has **364 pages**, with printed page numbers matching one-based PDF pages. Its SHA-256 is:

```text
8974902d109d6e63672d7c490bde9ccf052410503d9cfa768237154fbc5e3d87
```

The source is licensed under **CC BY 4.0**. The required attribution and changes notice are embedded in the manifest and retained in `ATTRIBUTION.md`. The C++/Python application code remains under the repository's BSD-3-Clause license. The PDF, extraction cache, and rendered source pages are not distributed in the pack.

## Delivered data

The generated catalog contains **678 records**:

| Kind | Count | Coverage |
|---|---:|---|
| Class | 12 | All SRD classes, core traits, initial/multiclass training, primary-ability prerequisites, equipment choices, feat levels, twenty-level caster/proficiency/XP tables, class-specific progression |
| Subclass | 12 | Every SRD subclass, **58** sourced feature descriptions, fixed subclass spell grants, and the four Circle of the Land spell tables |
| Spell | 339 | Every spell description in pages 107–175, class-list memberships, complete casting headers and text, component cost/consumption metadata, and source discrepancies retained |
| Feat | 19 | The SRD's 17 named feats; Magic Initiate is represented by three list-specific records, accounting for the additional two records |
| Invocation | 28 | All SRD invocation descriptions, Warlock-level and invocation dependencies, repeatability, and relevant cantrip prerequisites |
| Metamagic | 10 | All SRD Metamagic options with sorcery-point costs and descriptions |
| Creature | 70 | **64** non-Swarm Beasts with CR at most 1, plus **6** non-Beast Pact of the Chain forms; the seventh special Chain form, Venomous Snake, is already in the Beast set |
| Species / lineage | 9 / 24 | All legacy species and lineage choices, plus level-5 lineage spells and relevant character-level scaling metadata |
| Background / language / skill | 4 / 18 / 18 | All named SRD backgrounds; nine selectable Standard Languages plus nine Rare Languages; Common remains automatic; all eighteen skills |
| Weapon / armor / shield | 38 / 12 / 1 | Legacy SRD combat equipment data preserved, including its typed properties |
| Tool / gear | 37 / 27 | Existing tools and equipment plus items required by the ten added classes' starting kits; this is not a complete magic-item or general-equipment catalog |

Class entries contain **220 feature-grant records**, including repeated ASI opportunities and repeat grants whose prose is defined only once, such as later Bard/Rogue Expertise, Sorcerer Metamagic, and Warlock Mystic Arcana. Feature metadata identifies the intended `srd55-v2` handler and its family/name key. Descriptions and handler references alone are not evidence that the handler was executed; the manifest retains experimental status.

## Original pages and numerical checks

| Class | Core / progression page | Feature and subclass pages | Independently checked boundary examples |
|---|---:|---|---|
| Barbarian / Path of the Berserker | 28 | 28–30 | Rage uses 2/3/4/5/6 at 1/3/6/12/17; Rage damage +2/+3/+4 at 1/9/16 |
| Bard / College of Lore | 31 | 31–35 | Inspiration d6/d8/d10/d12 at 1/5/10/15; 22 base preparations at 20 |
| Cleric / Life Domain | 36 | 36–40 | Channel Divinity 2/3/4 at 2/6/18; 5 base cantrips and 22 preparations at 20 |
| Druid / Circle of the Land | 41 | 41–46 | Wild Shape 2/3/4 at 2/6/17; known forms 4/6/8 at 2/4/8, CR limits 1/4, 1/2, 1 |
| Fighter / Champion | 47 | 47–49 | Attacks 1/2/3/4 at 1/5/11/20; masteries reach 6 at 16; extra ASI opportunities retained |
| Monk / Warrior of the Open Hand | 50; core starts 49 | 49–52 | Martial Arts d6/d8/d10/d12 at 1/5/11/17; movement bonus reaches 30 feet at 18 |
| Paladin / Oath of Devotion | 53 | 53–57 | Spellcasting starts at 1 with two first-level slots; level-20 slots 4/3/3/3/2 |
| Ranger / Hunter | 58; core starts 57 | 57–61 | Spellcasting starts at 1; level-5 slots 4/2; 15 base preparations at 20 |
| Rogue / Thief | 62; core starts 61 | 61–64 | Sneak Attack reaches 10d6 at 19; expertise grant at 6 and additional ASI at 10 retained |
| Sorcerer / Draconic Sorcery | 65; core starts 64 | 64–70 | Initial preparations 2, then 4 at 2; Metamagic selections 2/4/6 at 2/10/17 |
| Warlock / Fiend Patron | 71; core starts 70 | 70–76 | Invocations 1 at 1, 3 at 2, 10 at 18; Pact slots 3 at 11 and 4 at 17, maximum slot level 5 |
| Wizard / Evoker | 77 | 77–82 | 25 base preparations and slots 4/3/3/3/3/2/2/1/1 at 20 |

The [global progression and multiclass tables, pages 23–26](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=23), and all twelve class progression tables were compared to rendered source pages. Core trait packages were checked against the class text, including restricted Monk/Rogue martial-weapon training and the different initial/multiclass proficiency packages. Each generated class owns twenty nine-column slot rows; Pact Magic additionally has separate `pactSlots` and `pactSlotLevel` arrays.

### Wizard book lifecycle source check

Printed pages **78, 79, and 104** were visually rechecked on September 8, 2026 against the official 5.2.1 PDF for physical spellbook implementation. Page 78 distinguishes discovering/copying a new spell (**2 hours and 50 GP per spell level**) from copying the Wizard's own book or reconstructing prepared Wizard spells after loss (**1 hour and 10 GP per spell level**). It supplies the initial book through Spellcasting, including a gold-start character; later blank-book acquisition has no invented catalog price. Normal research, Savant grants, and external acquisitions remain separate historical inputs, while each physical book retains its own actual contents.

Page 78's reconstruction procedure depends on Wizard preparations surviving loss. Its class-feature rule also identifies always-prepared Wizard-feature spells as Wizard spells. Page 79 supplies the book-study conditions for Memorize Spell and Spell Mastery changes; page 104 distinguishes spells prepared in the mind from other sources of casting access. These sources support preserving accepted preparations, Mastery, and Signature Spells while withholding unavailable book rituals and study, and they do not turn origin-only Wizard-list spells into Wizard-class book acquisitions.

Independent transaction examples are **Shield + Misty Step = 3 spell levels = 180 minutes + 30 GP** for an owned backup, and **two first-level prepared spells = 120 minutes + 20 GP** for reconstruction. Recovered originals retain their earlier written contents rather than inheriting later replacement-book research. Existing scroll checks and destruction still follow page **244**. The engine uses the selected supported Wizard profile's actual casting-slot rows to establish eligible ranks and Savant acquisition timing, preserving intentional compatible content differences.

Physical state is initialized explicitly with exact inventory identity. Older untracked losses that lack sufficient content history remain an explicit migration limitation; no free replacement contents are inferred. The published 100-page book size does not establish how many pages a spell occupies, so no capacity formula is extrapolated. See `srd55-lifecycle.md` for actions, source boundaries, and focused regression scope. This addition does not declare the broader SRD lifecycle or publication fully covered.

Class spell tables are on **33–35, 38–40, 44–45, 55–56, 60–61, 67–69, 74–76, 79–82**. Descriptions and casting metadata are on **107–175**. Feats are on **87–88**, Metamagic **66–67**, invocations **72–74**, and the selected creature stat blocks are taken from **254–364**. Creature extraction retains the six scores, actual saving-throw totals, skills, AC, HP, movement modes, CR, type, size, and full stat-block text. The Ape fixture independently checks Strength **16**, Athletics **+5**, and CR **1/2** from page 344.

## Published spell-list disagreements

Two source disagreements were found and visually verified. They are not silently treated as extraction errors:

| Spell | Class-list tables | Spell-description header | Version-2 handling |
|---|---|---|---|
| **Mind Spike** | Warlock and Wizard; absent from the Sorcerer level-2 table on page 68 | Lists Sorcerer, Warlock, Wizard on [page 149](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=149) | Retains `tableLists`, `descriptionLists`, `listSources`, and `listDiscrepancy`; selectable `lists` is the union of the two published membership sources |
| **Phantasmal Force** | Absent from the Bard, Sorcerer, and Wizard class-list tables | Lists Bard, Sorcerer, Wizard on [page 151](https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=151) | Same explicit policy; the description supplies the spell's published classes and original page reference |

This is a documented editorial policy for the version-2 data, not a claim that Wizards has issued errata. The legacy pack is unchanged: its Wizard level-2 list remains the **35** entries in the original table. Version 2 has **36** because the separately published Phantasmal Force header is retained. The discrepancy fields should remain visible to source inspection and must survive later regeneration.

Other extraction details requiring explicit handling:

- **Minor Illusion** has “Illusion” in both its name and school. The parser takes the final school column, preserving the full name and `srd55:minor-illusion` ID.
- **Barkskin**, page 112, uses singular “Component” in its casting header; both singular and plural forms are accepted.
- Printed component prices use forms such as **50+ GP**. Chromatic Orb preserves its 50 GP minimum diamond cost; Find Familiar preserves consumed incense worth at least 10 GP.
- `materialConsumed` means consumption specified directly by the casting components. `materialConsumption.mode` distinguishes on-cast, conditional, and not-consumed cases. Forbiddance preserves its conditional last-casting consumption in the full text and marks the condition explicitly.
- **True Strike** requires a weapon attack, not a ranged/melee spell attack. Its metadata uses `attackType: "weapon"`, `requiresAttackRoll: true`, and a Self range. A cantrip-specific invocation must use the relevant eligibility fields rather than assuming that every damaging cantrip has a ranged spell attack.
- Rare language IDs use `srd55:language-<slug>` so Abyssal and Infernal language records cannot overwrite the existing Tiefling lineage IDs. Cross-kind ID collisions now stop generation; tests check the exact lineage/language kinds and the total of 24 lineages.

## Schema alignment and limitations

The version-2 classes preserve the actual legacy key names **hitDie, fixedHp, saves, skills, skillCount, armorTraining, weaponTraining, kits**. They add `rulesProfile`, `primaryAbilities`/`primaryAbilityMode`, `multiclassSkillCount`, `multiclassSkills`, `multiclassTraining`, tool-selection metadata, `featLevels`, `features`, `progression`, and a `casting` object. `casting` owns the twenty-level `cantrips`, `prepared`, `slots`, `pactSlots`, and `pactSlotLevel` arrays plus casting ability, list, resource kind, learning mode, and replacement cadence.

Dynamic starting-kit choices are explicit `kits.<option>.choices` records. Bard instruments and Monk tool/instrument choices are not fabricated zero-cost objects. Physical staff focuses reference their Quarterstaff attack profile through `weaponProfile`; the Wizard's staff focus is not duplicated as a second physical Quarterstaff in its version-2 kit.

Feat records include `prerequisites`, `repeatable`, and, where applicable, `abilityIncrease` with points, maximum per ability, score cap, and eligible abilities. Invocations include the Warlock level and prerequisite invocation IDs. Fixed subclass spell grants and Circle of the Land terrain spell tables reference canonical spell IDs. A source-resolved validator must reject missing or wrong-kind references before the pack becomes usable.

Every spell has `effectCoverage: "reference-only"`. That describes the spell's effect text in this content layer: it does not claim that casting metadata, class membership, or character-building spell eligibility are unimplemented in the module, and it does not claim that the complete encounter effect has been automated. Full creature text likewise supports forms/companions without implementing a general combat simulator.

This delivery does **not** contain the entire magic-item catalog or every general equipment item. Class-specific feature execution, active effects, acquisition/rest/resource commands, multiclass evaluation, and GUI behavior are owned by their separate implementation/tests. Updating a catalog must not upgrade those implementation coverage claims automatically.

## Rebuild and verification

Use the checked-in generator with an external copy of the exact verified PDF:

```sh
python3 scripts/build-srd55-v2-data.py \
  --pdf /absolute/path/to/SRD_CC_v5.2.1.pdf \
  --legacy data/packs/srd55-core/content.json \
  --out data/packs/srd55-core-v2
```

The generator requires `pypdf`, rejects a PDF whose hash differs from the verified source, writes UTF-8 JSON, and checks spell-description coverage, class table shape, identifiers, and cross-kind collisions. Fresh extraction and cache-based generation were compared byte-for-byte for all three distributed pack files. Its optional `--cache /tmp/srd-pages.json` stores extracted source text outside the distributed pack and is tied to the PDF digest. Normal application startup uses only local generated JSON and does not require Python, the PDF, a network connection, or an account.

`tests/test_srd55_v2_content.cpp` independently checks record counts, exact-version attribution, all class table shapes, selected published numeric boundaries, feat/invocation prerequisites, canonical references, complete spell header metadata, both source disagreements, collision regressions, and creature stats. The isolated compiled Catch2 run passed **13,094 assertions in six test cases** during this data handoff. The tests belong in the repository's integrated test target; the separate module/feature tests determine whether the data is correctly executed.

## Imported profile acceptance — September 8, 2026

The profile-dispatch regression uses mechanically equivalent copies of the already sourced twelve class and twelve subclass definitions. The fixture changes their content IDs, display names, publisher, and record-level sources, then exercises folder validation, installation, exact-version resolution in both pack orders, and the public evaluator at levels 3 and 20. Resource capacities and creation-feature fields match the existing definitions, while saves and sheets retain the imported identities and provenance. This is an equivalence test of the content contract, not new source verification or additional rules coverage.

The Fighter anchors retain the existing SRD pages 47–49 expectations: two Second Wind uses at level 3, spending one leaves one, and a Short Rest restores one. Imported Fighter advancement preserves its class identity and accepted history. Imported Warlock advancement records an invocation's acquisition level and remains valid through a subsequent rest and save/reopen. A separate explicitly homebrew fixture changes the selected Fighter's level-1 Second Wind capacity to three and its attacks to two, proving those interpreted progression columns come from the selected record rather than the stock ID; those changes are fixture rulings, not published SRD rules.

Unsupported feature schedules, subclass-to-class profile bindings, unimplemented progression variations, malformed rows and creature statistics, and unsupported executable bindings fail validation or resolution before installation. The supported variation limits are listed in `docs/content-packs.md`; rejecting a variation is not an implementation claim for it. No source-publication, module, pack, or save version is changed by this correction.
