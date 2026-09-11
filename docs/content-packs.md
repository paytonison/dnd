# Content packs

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

A pack is a folder containing `manifest.json` and one or more JSON data files. Start from `templates/content-pack`. The manifest's JSON Schema is `docs/content-pack.schema.json`; the executable validator also checks the edition's supported mechanical shapes. A schema-valid manifest alone does not prove a mechanically valid pack.

```sh
./build/dnd-cli validate-pack templates/content-pack
```

Import a folder through **Sources → Import content pack**. The application validates all declared data files, stages a copy of only those files and the manifest, validates that copy, then renames it into its local content folder. A pack version cannot overwrite an installed version. Failed validation leaves the catalog unchanged. Enable the imported version in the character's source-selection dialog. Dependencies must be explicitly enabled at their exact versions.

Required manifest fields:

| Field | Meaning |
| --- | --- |
| `schemaVersion` | Integer `1` |
| `id` | Lowercase letters, digits, hyphens; starts with a letter |
| `version` | Exact `major.minor.patch` |
| `edition` | A supported compiled edition identifier |
| `moduleVersions` | Optional exact module versions; absence means `1.0.0`. Every advertised configuration must validate, including dependencies. |
| `name`, `publisher` | Human-readable identity and attribution |
| `origin` | `official`, `third-party`, or `homebrew`; describes the source, not endorsement |
| `license` | Object with nonempty `id` and `text`; a `url` is recommended |
| `sources` | Nonempty array of publication references |
| `dependencies` | Array of `{ "id": "…", "version": "1.0.0" }` exact pins |
| `conflicts` | Array of incompatible pack identifiers |
| `dataFiles` | Unique relative `.json` paths inside this folder |

Every data file contains an array of entries. Entries require a namespaced `id`, `kind`, `name`, and `source` with a publication and printed page or section. Namespaces separate independent publishers' content. A supported `replaces` string can name another enabled entry. Two replacements for the same target fail resolution; install order never selects a winner.

Edition-specific properties contain the mechanical facts. Read the bundled pack for the current supported shapes and the module's `validateContent` implementation for exact validation. B/X supports classes, armor, weapons, gear, languages, spells, and its specialized rule tables. **5E (2014)** uses edition ID `srd51`; **5.5E (2024)** uses `srd55`. The 5.5E modules support their classes, species, backgrounds, feats, equipment, languages, skills, and spells. The smaller 5E contract is described below. Adding ordinary content within a supported mechanic uses data. Inventing a new mechanic requires a C++ implementation and validation; arbitrary JSON properties are not executable rules.

The template is an original homebrew weapon using supported B/X properties. It intentionally changes the price and name of a mechanically ordinary hand weapon. Keep the source and license metadata accurate for your own content. Do not place book scans, artwork, or copied copyrighted prose in distributable packs.

Character saves pin exact enabled pack versions. Disabling a source retains choices from that source; the evaluator explains that they are unavailable. Re-enable the source to restore their meaning. Importing a newer version does not silently migrate saved characters.

## 5E (2014): `srd51` module 1.0.0

The first pack is `data/packs/srd51-core`, ID `srd51-core`, version `1.0.0`, with `edition: "srd51"` and `moduleVersions: ["1.0.0"]`. This is separate from both 5.5E packs. For a dependent homebrew pack, declare the exact dependency `{ "id": "srd51-core", "version": "1.0.0" }` and preserve the new pack's own ID, publisher, source and license metadata.

The mechanically active entry contract is enforced by `src/editions/srd51_content.cpp`, with the bundled content as the complete worked example. The manifest schema remains shared. Recognized entry kinds are `class`, `race`, `background`, `subclass`, `fighting-style`, `table`, `skill`, `language`, `armor`, `shield`, `weapon`, and `gear`. Each kind rejects unknown fields, missing required fields, unsupported profiles, and malformed types/bounds. New class, race, spellcasting, magic-item or feat mechanics are not accepted merely because their records can be parsed.

| Kind | Supported binding and constraints |
| --- | --- |
| `class` | `rulesProfile: "fighter"`; d10, fixed HP 6, maximum supported level 3; exactly three typed progression rows with increasing XP thresholds, proficiency +2, and supported Second Wind/Action Surge capacities. `creationId` must resolve to a creation table. Skills, saving throws, styles, and starting equipment use the supported data fields. |
| `race` | `rulesProfile: "human"`; six bounded ability increases, Medium size, speed and language grants. Other racial mechanics require additional profiles. |
| `background` | `rulesProfile: "acolyte"`; two skills, language choices, starting coins, equipment groups, and a descriptive feature. Description text does not execute additional mechanics. |
| `subclass` | `rulesProfile: "champion"`; level 3, critical minimum 19, and `classId` resolving to a Fighter profile. |
| `fighting-style` | `archery`, `defense`, `dueling`, `great-weapon-fighting`, `protection`, or `two-weapon-fighting`. Effects and scope follow the compiled handler; descriptions cannot introduce additional effects. |
| `table` | `rulesProfile: "creation"`; six standard-array scores, eight point costs corresponding to scores 8-15, and a point budget. |
| `weapon` | Simple/martial and melee/ranged categories, bounded supported dice, physical damage type, price, and recognized properties. Ranged/thrown profiles require ordered ranges; ammunition profiles require an `ammunitionId` resolving to gear. Versatile requires a one-handed melee profile. Lance/Net special mechanics are unsupported. |
| `armor`, `shield` | Armor category, base AC, price, Strength threshold and Stealth disadvantage; only heavy armor may set a nonzero Strength threshold. Shield defines its supported AC bonus and price. |
| `gear` | Nonempty `description`, integer `costCp`, boolean `purchasable`; optional `purchaseQuantity` and descriptive `contentsSummary`, as below. |

Supported profiles dispatch independently of literal stock IDs, preserving selected record identities and sources. A differently named Fighter can retain the Fighter features and compatible Champion choice without pretending to be `srd51:fighter`. The resolver checks class/subclass/creation bindings and references to skills, styles, languages, equipment and ammunition against their required roles. Shared replacement checks still apply: replacements must preserve kind and the target's supported shape; conflicts and exact-version resolution remain independent of pack order.

Starting equipment uses `equipmentGroups`: each group has a unique simple `key`, a `name`, and choices in `options`. An option has `id`, `name`, and an `items` array; repeated item IDs grant repeated pieces. Class options may also declare zero to two `martialWeapons` choices. Background options cannot add martial-weapon choices. References must resolve to equipment kinds before their mechanics are available.

For purchasable gear, `costCp` is the price of one purchase bundle. Optional `purchaseQuantity` is an integer from **1 through 100**, default **1**, specifying how many pieces that purchase grants. A starting-equipment `items` occurrence still grants one piece; it does not multiply by the purchase bundle size. For example, the stock sling-bullet record uses `costCp: 4` and `purchaseQuantity: 20`, matching the published 20 bullets for 4 cp. The current character interface permits one purchase of each listed item or bundle; it is not a general transaction ledger.

Optional `contentsSummary` is nonempty sheet text describing an equipment pack's contents. It neither grants referenced items nor implements their weight, capacity, consumption or other effects. Keep those limits visible when authoring content. [The source ledger](sources-srd51.md) documents exact source pages, corrections and reuse terms; [the 5E guide](srd51.md) describes character inputs and actions.

## 5.5E (2024): `srd55` module 2.0.0

The 5.5E `2.0.0` pack is stored in `data/packs/srd55-core-v2` and pins module version `2.0.0`. Its schema adds 20-row class/casting progressions, typed feat prerequisites and increases, invocation/Metamagic records, and creature forms. It is separate from the frozen first-slice pack. Only supported C++ mechanics profiles execute; spell effect text is a source reference, not a combat automation program.

5.5E module `2.0.0` class and subclass definitions dispatch by `rulesProfile`, while the selected namespaced IDs, names, publishers, and source references remain the content's own. A class with `rulesProfile: "fighter"` uses `/features/fighter`, `/subclasses/fighter`, and `fighter:*` resource paths even when its content ID is different. A subclass's `classId` must resolve to the class profile its compiled subclass handler supports. A character cannot combine two differently named definitions of the same class profile as separate multiclass tracks.

The supported profile is currently a complete compiled feature schedule: retain its feature bindings and acquisition levels. Unknown bindings, additional executable properties, missing features, and moved feature acquisition are rejected, rather than silently ignored. Display names, descriptions, source references, supported equipment/casting counts, and interpreted class progression values may differ. Each profile requires its recognized progression columns with exactly twenty typed, bounded values. Variations in columns still coupled to a compiled lifecycle schedule are explicitly unsupported: Druid `knownForms`/`maxFormCr`, Paladin `layOnHands`, Rogue `sneakAttack`, Sorcerer `metamagicCount`/`innateSorcery`, and Warlock `invocations`/`pactSlotLevel`/`pactSlotCount`. Casting family, ability, list, acquisition/replacement cadence, subclass spell-grant tables, and total-character proficiency retain the supported profile's binding. Extending these contracts requires corresponding evaluator and persistent-action support. Feat, Invocation, and Metamagic executable identities remain the supported stock identities (including explicit replacements of those identities); alternate standalone bindings are rejected pending profile-based lifecycle support. Unsupported top-level `mechanics` objects are rejected, and creature movement, saving throws, and skills require recognized names with bounded integer values.

Species traits also retain their supported stock identities; standalone alternate species IDs are rejected until species profile dispatch is implemented. Explicit compatible replacements preserve their own names, source references, and publisher metadata while retaining the replaced species' executable traits. Dragonborn and Goliath progression payloads must match the compiled schedules; other species do not accept a progression object. Supported scalar properties such as Speed, darkvision, and HP per level remain data-driven.

Lineage `chooseCantrip` must be a boolean. The legacy `freeUses: 2` field is accepted only alongside `level1Spell` and records the original first-level count; the supported handler derives that spell's actual free uses from total-character proficiency. Alternative counts are rejected instead of being ignored. Invalid lineage controls are diagnosed at the pack record and field before installation, resolution, or character evaluation.
