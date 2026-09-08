# Architecture

The application is a C++20 rules engine with a Qt 6 Widgets desktop client. The rules targets do not link Qt. `dnd_engine` owns the public document/evaluation contract, `dnd_editions` supplies compiled rule implementations, `dnd_rules` links them, `dnd_content` loads packs and persists files, and the CLI and desktop client consume these interfaces.

`CharacterDocument` stores an edition identifier and exact module version, pack pins, identity, campaign settings, JSON choices, advancement records, accepted rolls, current resources, and explicit overrides. JSON choices deliberately have no common race/class/level structure: B/X uses race-as-class, while the experimental SRD module uses independent species, background, and class choices. Future modules can represent multiclassing, dual-classing, prestige classes, and powers without changing the document envelope.

`resolveRuleset` requires exact versions and rejects missing dependencies, incompatible editions, dependency cycles, duplicate IDs, and replacement conflicts. Resolution sorts enabled packs by identifier, so installation order never decides precedence. A replacement explicitly names its target, has its own unique identifier, and retains its source metadata. Replacement chains are unsupported and rejected.

`evaluate` is deterministic. It consumes the document and already resolved content, with no file access, random numbers, clock, or GUI state. Edition modules return dynamic builder stages, choices with eligibility reasons, calculations with source references, sheet sections, and validation messages. An incomplete document is an ordinary draft. Invalid structured inputs produce validation errors rather than ending the application.

Only explicit dice actions use randomness. The desktop records the dice and accepted totals, and updates the corresponding creation inputs. Reopening, switching builder stages, and refreshing never reroll. Current HP, money, and similar session resources have a separate editor and do not alter their creation-time inputs.

Calculation overrides replace only the named final result. They do not rewrite the underlying input or cascade into other calculations. The normal calculation, effective result, and reason are shown together. Choice exceptions can waive an advertised eligibility restriction on an existing supported option; they cannot introduce missing content, change edition, waive malformed inputs, or repair a failed ruleset. Advanced mode controls visibility, not the existence or effect of saved settings and overrides.

Saving validates the document envelope and writes to a uniquely created file in the destination directory, flushes it, and atomically replaces the destination. POSIX builds also flush the directory; Windows uses `FlushFileBuffers` and `MoveFileExW`. Autosaves use a separate `.autosave` file. Unsupported save versions and unavailable module versions open in inspection mode, retaining the parsed original JSON and preserving the original file. The GUI likewise protects characters whose exact packs cannot be resolved.

The desktop renders the same evaluation to its live sheet, explanations, printing, and PDF. The CLI can validate a pack, list edition modules, create drafts, evaluate saved documents, and write HTML sheets. No account or network connection is needed to use installed rules and local character files.
