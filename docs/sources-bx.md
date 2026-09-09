# Original B/X source and verification ledger

The bundled `bx-core` pack implements character creation and the complete **published 1981 Basic/Expert class tables**: cleric, fighter, magic-user, and thief through level 14; dwarf through 12; elf through 10; halfling through 8. This ledger records what was checked against the original books, what the builder calculates, and the few places that require an explicit interpretation or DM decision. It does not certify a retroclone or the 1983 Mentzer books as equivalent sources.

## Source identity and inspection

Sources were inspected on **2026-09-07/08**. The plan's official storefront previews were opened first. Full original scans were then located online, rendered for visual reading, and compared with overlapping official preview pages. Text extraction from the full scans was empty; the implementation therefore used the visible printed tables, not reconstructed OCR columns. The scans and rendered pages were kept in a temporary directory outside the repository and are not application assets.

| Reference | Original identity and inspected overlap | Availability |
|---|---|---|
| [Moldvay Basic official preview](https://d1vzi28wh99zvq.cloudfront.net/pdf_previews/110274-sample.pdf) | Tom Moldvay, Basic Rulebook, 1981. B5 creation, B6 adjustments, B7 modifiers, B8 tables, B9 class descriptions were compared with the corresponding full scan pages. | Partial preview, 12 PDF pages; not the complete Basic book. |
| [Cook/Marsh Expert official preview](https://d1vzi28wh99zvq.cloudfront.net/pdf_previews/110792-sample.pdf) | X1 identifies David Cook with Steve Marsh, 1981. X5's cleric, dwarf, elf, and turning tables were compared visually with the full scan. | Partial preview, 6 PDF pages; not the complete Expert book. |
| [Original Basic full scan](https://www.americanroads.us/DandD/DnD_Basic_Rules_Moldvay.pdf) | B1 names Moldvay, shows the 1981 copyright, first printing January 1981, ISBN 0-935696-48-2. B5-B9 match the official preview's content and table positions. | Third-party mirror of the original primary document, 68 PDF pages. The host's redistribution rights are not established by this project. |
| [Original Expert full scan](https://www.americanroads.us/DandD/DnD_Expert_Rules_Cook.pdf) | X1 names Cook with Marsh, shows 1981 copyright and ISBN 0-935696-29-6. X1 and X5 match the official preview. | Third-party mirror of the original primary document, 68 PDF pages. X12 and X13 are interchanged in PDF order; references always use the visible printed page number. |

Downloaded-file SHA-256 identifiers make the inspected source snapshots reproducible without distributing the PDFs:

| Snapshot | SHA-256 |
|---|---|
| Basic preview | `6e08b371c419bd7e8c66dc92cca777d8866fa7ed22daccbdb455432e84201463` |
| Expert preview | `1b21e16396036b2a22e5abb98030f5a769f71ebf4337ff95e6582319a5572d64` |
| Basic original scan | `3431e75017118f1ae52914f6718f29b4aaf46790316a83b248a180810371c119` |
| Expert original scan | `0c96b36c8b2c1837b439b29169f2cfce14028c1c5eb1ada4a3fede2da174fb3e` |

## Verified character mechanics

| Printed source | Visual checks and implemented behavior | Acceptance evidence |
|---|---|---|
| B5-B7 | Six accepted 3d6 totals; legal donor abilities; two donor points per prime-requisite point; reductions stop at 9; Constitution and Charisma cannot be exchanged; per-hit-die minimum of 1 HP. Strength, Dexterity, Constitution, Wisdom, Intelligence, and Charisma tables retain their different applications. | Eligibility/adjustment boundaries, all prime-requisite XP percentage bands, low-Constitution minimum HP, high-Constitution post-ninth-level HP checks. |
| B8-B10, X5-X8 | All seven classes' XP thresholds, hit dice, post-ninth-level fixed HP, saving-table intervals, spell slots, requirements, abilities, and printed demihuman limits. Human profiles stop at the last full table, level 14. | Every level of every class, all first levels and caps, XP and HP boundary checks. |
| B9-B10 | Dwarf CON 9; elf INT 9; halfling DEX 9 and CON 9. Elf XP +5% for STR/INT 13, +10% for STR 13 and INT 16. Halfling XP +5% for either STR or DEX 13, +10% for both. Class-specific weapon, armor, and shield restrictions. | Threshold and exception tests; illegal armor, shields, swords, longbows, and two-handed swords. |
| B7, B9-B11, B13 | Alignment and alignment language; dwarf and elf native languages; Intelligence additional language counts and literacy; Charisma reaction/retainer tables. | Morgan example and language-count/type/duplicate validation. See halfling interpretation below. |
| B12, B20, B27; X2, X9, X25 | Starting 3d6 × 10 gp; standard adventuring purchases and quantities; descending armor class; shield and Dexterity; standard d6 damage, optional variable weapon damage; range adjustments; armor-category and detailed coin encumbrance. | Independently totaled B13 purchases, detailed weight, weapon restrictions, quantities, budget errors, default/optional damage. |
| X24, X26 | Every class's five saving categories and all level bands. Original attack matrix, including repeated 20s, is used directly. Strength and Dexterity are applied separately; Wisdom remains a conditional magical-attack adjustment. | All class saving throw levels; fighter attack boundaries through 14 and negative-AC repeated-20 cells; Wisdom versus spell and breath comparison. |
| B8, B10; X6, X8 | All fourteen thief rows, backstab, read-languages milestone, scroll milestone, and follower/hideout note. The printed level-6 Hide in Shadows value is **36%**. Hear Noise is a d6 threshold; Pick Pockets exceeds 100% at high levels before victim adjustment. | Level 6 and 14 table exceptions, hear-noise denominator and backstab checks. |
| B9; X5, X7 | Eleven turning rows, including automatic turning/destruction and unavailable targets. Class ability text identifies the separate 2d6 affected-HD result. | Low, middle, and all-destroy turning rows. |
| B15-B18; X5-X6, X11-X18 | Canonical catalog of **34 cleric and 72 magic-user spells**, spell levels, reversible forms, daily counts, learned-spell counts, and memorization. Magic-users/elves hold exactly the number of spells shown for each daily spell level; Read Magic is not a free additional spell. Each daily slot can hold the same learned spell. | All caster levels' slot arrays; exact spellbook count, unavailable learning, duplicated memorization, reversed forms, elf's lack of sixth-level spells. |
| B7, B22 | Prime requisite modifies a new XP award, not historical XP. A single adventure stops below the threshold for gaining a second level. The character's selected level changes only when the user records advancement. | 5,000 XP fighter award with +10% produces 5,500 adjusted XP but 3,999 retained at first level; saved XP remains unchanged by later prime-requisite edits. |

Sources for calculation explanations are attached to the calculated values themselves. The source browser displays the source attached to each content entry. This remains useful offline: publication and printed-page references are local metadata; following an external URL requires connectivity.

## Independent worked acceptance examples

`tests/test_bx.cpp` keeps literal expected XP, spell-slot, and saving-throw tables separately from the content pack. It does not obtain expected arithmetic by rereading the pack's result. Fixture setup reads spell identifiers only to select a legal spellbook.

- **B13 Morgan Ironwolf:** raw STR 15, INT 7, WIS 11, DEX 13, CON 14, CHA 8; exchange two Wisdom for one Strength; accepted HP die 5; money roll 11. The result is STR 16/WIS 9, +10% XP, 6 HP, chain + shield + Dexterity AC **3**, equipment **108 gp**, remaining **2 gp**, melee target **8** against AC 9, missile target **9**, reaction **-1**, and maximum **3** retainers. The B20 weight example totals 670 coins without her two unspent gold; the saved-inventory calculation includes them, producing **672**, with the same **60 feet/turn** movement.
- **Dwarf 12, CON 18, nine accepted rolls of 2:** nine gains of 5 plus three fixed gains of 3 produce **54 HP**. Constitution is not applied to the fixed levels.
- **Fighter 10, CON 3, nine accepted rolls of 2:** each rolled level reaches the minimum of 1 HP, then the tenth level adds a fixed 2, producing **11 HP**.
- **Cleric 6:** slots are **2/2/1/1/0**, as printed on Expert X5. The smaller Basic B18 table exists for the Basic book's higher-level NPC guidance; the complete Expert progression governs the combined B/X implementation.
- **Elf 10:** slots **3/3/3/3/2/0**, nine d6-based HP gains plus a fixed 2, and no sixth-level spell choice.
- **Thief 14:** Pick Pockets **125%** before the victim-level adjustment and minimum failure chance; Hear Noise succeeds on **1-5 on d6**.

Earlier B/X validation milestone: **12 test cases, 9,866 assertions** passed on the development Mac after the implementation and source corrections. The authoritative command is `build/dnd_tests '[bx]'`; the total may grow as regressions are added. GUI workflow and persistence tests are owned by the shared application test suite rather than this ledger.

## Explicit interpretations and DM decisions

These are not presented as additional verified book rules:

1. **Beyond level 14 — deferred at the user's request:** On September 8, 2026 the user explicitly chose to keep human levels above 14 unavailable for now. X7 and X8 were visually rechecked against the original Expert snapshot identified above. Human advancement through 36 remains the published maximum; 14 is the current **Printed-table coverage limit**, not a normal human limit. The module continues to reject higher inputs explicitly while preserving them.

   X8 gives each additional level +100,000 XP/+1 HP for Cleric, +120,000 XP/+2 HP for Fighter, +150,000 XP/+1 HP for Magic-user, and +120,000 XP/+2 HP for Thief. Constitution does not modify the fixed gains. X24's existing save bands already extend through Cleric/Thief 16 and Fighter/Magic-user 15, but do not specify later saves. X5's final turning row is explicitly **11+**, with **D** for all eight published undead categories; continuing that row does not require an invented turning progression.

   Attack continuation has an original-source conflict: X8's examples give Fighter 16 against AC 2 a target of **7**, then Fighter 19 a target of **5**. X26's printed Fighter 13–15 row gives **8** against AC 2; applying X8's stated two-point improvement to that row would instead produce **6** and **4**. Both pages were visually checked, including a higher-resolution render of X26. No resolution of this conflict has been silently chosen. Later spell and thief-ability progressions are suggested DM development pending Companion, not complete tables. A future continuation policy must explicitly address those gaps and the attack conflict. No BECMI, retroclone, or invented level-36 table has been substituted.

2. **Halfling native language:** B10 does not state a free Halfling-language grant, while B13 expressly identifies dwarves and elves as receiving extra languages through class abilities. The module grants halflings Common and their alignment language, and offers Halfling in the extra-language catalog. This deliberately follows the visible original text; the inferred absence of a grant is an interpretation, not a positive quotation that halflings cannot speak it. A campaign that grants it should record a DM override.
3. **Class choice precedes exchange:** B5 orders class selection before B6 adjustments. Minimum eligibility is therefore checked on accepted rolled scores; an elf with rolled INT 8 cannot become eligible merely by entering a post-selection adjustment. This ordering interpretation can be overridden with a documented choice override.
4. **Two prime requisites:** Elf and halfling use the bonus conditions written in their class descriptions. No additional low-score penalty is inferred for them where their special descriptions give none. The ordinary single-prime-requisite classes use the complete B7 penalty/bonus table.
5. **Fractional XP:** B/X gives percentage adjustments but does not provide an explicit universal rounding instruction on the inspected pages. The award preview truncates fractional XP toward zero, states that convention in its explanation, and leaves acceptance explicit.
6. **Unlisted weights and weapon sizing:** B20 delegates weights not on its table to the DM. Javelin, lance, and sling carry an explicit unknown listed weight, not an invented weight. Detailed encumbrance requests a weight and reason for them. Dwarf/halfling longbow and two-handed-sword exclusions are explicit on B9/B10. Other weapon sizing, including fitting or cutting down a weapon, remains a DM judgment; the builder does not manufacture an extra blanket ban on every two-handed weapon.
7. **Higher-level creation wealth:** The inspected creation rule supplies 3d6 × 10 gp and does not provide a universal higher-level wealth budget. An additional wealth grant is marked as a house ruling and requires its own reason. Equipment is purchased from the resulting recorded creation budget.
8. **Clerical reversals and domains:** Clerics can reverse eligible spells at casting time, subject to alignment/deity judgment on X11. The UI may show a reversed selection as a planned form; it does not invent a second learned spell. Class strongholds, followers, scroll mishaps, and religious obligations are shown as sourced notes or table results; their campaign adjudication is not an automated simulation.
9. **Starting gold and basic encumbrance:** Detailed encumbrance includes unspent creation gold as coin weight. Basic encumbrance uses the selected armor category plus the explicit other-treasure input; its treatment of that treasure is shown in the calculation explanation.

## Published options and scope

The campaign/Advanced settings expose variable weapon damage (B27/X25), individual initiative (B7/B23), first-level reroll permission for a result of 1 or 2 (B6), the X4 two-handed/crossbow timing rules, and the B20 encumbrance method. Enabling a reroll option does not reroll the saved character. Accepted dice results and advancement inputs remain saved, while current HP and other resources are separate document data.

The module is a **character builder**, not a combat referee. It supplies spell names, levels, reversible forms, preparation, and source references; it does not copy spell descriptions or resolve every spell's effects in combat. It does not generate magic items, simulate domains, roll followers, or extrapolate beyond the full original progression tables. Additional content can supply supported class profiles and tables, equipment, armor, weapons, languages, and spells; a genuinely new mechanic requires a module extension.

## Content license and distribution

`manifest.json` separates original publication identity from the independent implementation. The pack contains game-mechanical facts, names needed to identify choices, and newly written explanations. Its `origin: official` means the **source publications** are official; the application and encoded pack are unofficial and are not endorsed by TSR or Wizards of the Coast. The original publications remain copyrighted by their respective rights holders. No scan, original art, or copied book prose is bundled. Application code and original project text retain the repository's BSD-3-Clause terms; the manifest does not purport to license the original books.
