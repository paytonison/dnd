# Content packs

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

Edition-specific properties contain the mechanical facts. Read the bundled pack for the current supported shapes and the module's `validateContent` implementation for exact validation. B/X supports classes, armor, weapons, gear, languages, spells, and its specialized rule tables. The SRD module supports its classes, species, backgrounds, feats, equipment, languages, skills, and spells. Adding a supported equipment item or spell name/list entry uses data. Inventing a new mechanic requires a C++ implementation and validation; arbitrary JSON properties are not executable rules.

The template is an original homebrew weapon using supported B/X properties. It intentionally changes the price and name of a mechanically ordinary hand weapon. Keep the source and license metadata accurate for your own content. Do not place book scans, artwork, or copied copyrighted prose in distributable packs.

Character saves pin exact enabled pack versions. Disabling a source retains choices from that source; the evaluator explains that they are unavailable. Re-enable the source to restore their meaning. Importing a newer version does not silently migrate saved characters.

The SRD `2.0.0` pack is stored in `data/packs/srd55-core-v2` and pins module version `2.0.0`. Its schema adds 20-row class/casting progressions, typed feat prerequisites and increases, invocation/Metamagic records, and creature forms. It is separate from the frozen first-slice pack. Only supported C++ mechanics profiles execute; spell effect text is a source reference, not a combat automation program.

SRD module `2.0.0` class and subclass definitions dispatch by `rulesProfile`, while the selected namespaced IDs, names, publishers, and source references remain the content's own. A class with `rulesProfile: "fighter"` uses `/features/fighter`, `/subclasses/fighter`, and `fighter:*` resource paths even when its content ID is different. A subclass's `classId` must resolve to the class profile its compiled subclass handler supports. A character cannot combine two differently named definitions of the same class profile as separate multiclass tracks.

The supported profile is currently a complete compiled feature schedule: retain its feature bindings and acquisition levels. Unknown bindings, additional executable properties, missing features, and moved feature acquisition are rejected, rather than silently ignored. Display names, descriptions, source references, supported equipment/casting counts, and interpreted class progression values may differ. Each profile requires its recognized progression columns with exactly twenty typed, bounded values. Variations in columns still coupled to a compiled lifecycle schedule are explicitly unsupported: Druid `knownForms`/`maxFormCr`, Paladin `layOnHands`, Rogue `sneakAttack`, Sorcerer `metamagicCount`/`innateSorcery`, and Warlock `invocations`/`pactSlotLevel`/`pactSlotCount`. Casting family, ability, list, acquisition/replacement cadence, subclass spell-grant tables, and total-character proficiency retain the supported profile's binding. Extending these contracts requires corresponding evaluator and persistent-action support. Feat, Invocation, and Metamagic executable identities remain the supported stock identities (including explicit replacements of those identities); alternate standalone bindings are rejected pending profile-based lifecycle support. Unsupported top-level `mechanics` objects are rejected, and creature movement, saving throws, and skills require recognized names with bounded integer values.

Species traits also retain their supported stock identities; standalone alternate species IDs are rejected until species profile dispatch is implemented. Explicit compatible replacements preserve their own names, source references, and publisher metadata while retaining the replaced species' executable traits. Dragonborn and Goliath progression payloads must match the compiled schedules; other species do not accept a progression object. Supported scalar properties such as Speed, darkvision, and HP per level remain data-driven.

Lineage `chooseCantrip` must be a boolean. The legacy `freeUses: 2` field is accepted only alongside `level1Spell` and records the original first-level count; the supported handler derives that spell's actual free uses from total-character proficiency. Alternative counts are rejected instead of being ignored. Invalid lineage controls are diagnosed at the pack record and field before installation, resolution, or character evaluation.
