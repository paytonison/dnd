# GUI rules-module versions and current state

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application. See [the release policy](versioning.md).

The desktop treats an edition identifier and its rules-module version as a pair. New-character menu entries show both the module's name and exact version. Selecting one calls `newCharacter(edition, version)` and retains the module's default content-pack pins. The GUI does not select a newer module or a different pack version while opening an existing character. The no-version API remains the legacy default for existing callers.

The document header and identity pane display the saved module version. Legacy SRD module 1.0.0 continues to identify its limited fighter/wizard, level 1–3 scope. Expanded module 2.0.0 uses its own stages and labels. The edition-generated form does not impose that legacy level limit or a universal single-class model: ordered advancement rows and hit-point fields are ordinary JSON pointers.

## Content selection

The enabled-sources dialog includes packs compatible with both the document's edition and module version. A manifest without `moduleVersions` is compatible only with module 1.0.0. An existing missing or incompatible pin remains visible and selected until the user explicitly disables it. Pack versions are shown alongside pack identifiers in the content browser; content from another rules module is labeled accordingly.

## Explicit upgrade copies

`File → Create upgraded copy…` is enabled when a newer module is installed for the current edition. The GUI calls the shared `migrateCharacterVersion` API and displays the proposed sheet, migrated inputs, migration messages, and destination validation before offering to create the copy. It does not implement its own field transformations.

Accepting the preview creates a new unsaved document. The original filename is protected against saving the upgraded copy over it during this workflow. The original saved file and any recovery copy of its unsaved legacy inputs remain intact. Recovery for the new copy uses the untitled-document recovery location until the copy receives a new filename. Missing destination rules or an unsupported migration leave the current document unchanged.

## Dice requests

New modules describe explicit dice actions with `Evaluation.rollRequests`. Each request has an identifier, label, destination pointer within `document.choices`, category, die size, die count, and number of lowest dice to discard. The GUI applies the request as supplied, so different advancement events can use different class hit dice without GUI class-name guesses.

The dice dialog starts previously accepted inputs unchecked. The user must explicitly select an existing input to replace it. Accepted values are written to their choice paths; detailed records are stored at `document.rolls.requests[id]`, with replaced request records retained in `document.rolls.history`. Evaluating, editing an unrelated field, saving, opening, and printing never roll dice. Legacy module 1.0.0 retains its existing dice actions when it does not provide requests. New modules never fall back to legacy paths merely because their current request list is empty.

## Current resources and effects

`Field.scope` routes a form field to `document.choices` or `document.resources`. Current effects, forms, and other state fields use the same generic controls while staying separate from creation and advancement choices. Turning Advanced mode off hides advanced controls and preserves their values. Eligibility overrides in the choice-override dialog apply only to choice-scoped fields.

`Evaluation.resources` supplies remaining-use capacities, labels, recharge descriptions, and references. The resource dialog displays these definitions and changes only values the user edits. An absent resource starts as “Not tracked”; opening or accepting an untouched dialog does not initialize or refill it. Existing values beyond a newly reduced capacity remain preserved and visibly flagged until edited. Unknown stored resource keys are retained.

Resource editing and effect selection do not imply spending an activation cost, taking a rest, recharging other resources, or rolling a die. Those are separate explicit play-session decisions. The sheet and validation notes describe the resulting saved state.

## Validated character actions

`Character → Actions…` displays the current module's `Evaluation.actions`. Each action provides its label, description, availability reason, references, and form fields. All action-field pointers address `CharacterCommand.inputs`, regardless of the field's builder scope. The GUI contains no class-specific transaction rules.

The normal action form uses select, multiselect, integer, boolean, and text controls. `ActionDefinition.initialInputs` visibly seeds these controls, including existing selections for a permitted replacement; later edits are retained until the user changes actions. These are UI defaults, not an implicit merge into command inputs for API callers. A rare `json` field accepts structured JSON and blocks preview while its text is malformed. Unavailable actions remain inspectable but cannot be previewed or applied. Read-only action inputs display the module's explanation.

Preview calls the shared `executeCommand` API against a captured document and ruleset. It shows validation/effect messages, before-and-after values, the journal-entry count, and the proposed sheet. The live character, current resources, spell choices, funds, and history remain untouched while previewing or cancelling. Changing an input invalidates the preview.

Apply commits the exact valid candidate that was reviewed. If the live document changed after preview, the GUI rejects the candidate and requires reopening Actions against the current state. Failed or unavailable commands never partially modify the character. Successful transitions retain the module's journal entries and use the existing dirty-state, recoverable autosave, and atomic save workflow. `MainWindow::executeAction` exposes the same engine transition boundary for interface tests.

When the module marks a field `editable = false`, the builder displays its `readOnlyReason` and disables the corresponding input. GUI setters compare all locked paths before and after a proposed edit, so replacing an ancestor object cannot bypass the lock. Validated character actions remain able to perform their declared changes. This lets a module protect accepted advancement history while opening only the choices allowed by a pending advancement or replacement transaction.

The expanded Qt interface suite passed all 23 entries on September 8, 2026. It covers preview/cancel purity, exact-candidate application, stale-preview rejection, visible action defaults, interrupted-rest input, resource costs and recovery, accepted Hit Die results, command autosaves, Wizard copying with a coupled 5,000-copper-piece payment, owned-item acquisition/equipment/attunement, protected history fields, prefilled spell-replacement controls, and structured-input rejection.

A native macOS Actions check used an isolated copy at `output/action-qa/fighter-preview.dnd.json`. Preview showed a one-point HP cost and one proposed journal entry; cancelling left the character clean and its HP at 14. Repeating the preview and explicitly applying it produced current HP 13. Native Save and Open preserved HP 13, and file inspection confirmed exactly one `srd55.resources.spend` command entry with unchanged creation choices and ability-roll evidence. The original fixture remained unchanged. `output/action-qa/native-actions.json` records this check. No PDF or package regeneration was part of the Actions implementation.

## Verified rules-module 2.0.0 desktop and print workflow

On September 8, 2026, the native macOS app built in `build-gui-qa/Dungeoning a Dragon.app` opened the complete level-20 Wizard fixture through the macOS Open panel. The header identified module 2.0.0 and reported Ready to play. The native calculation inspector showed Wizard spell-save DC 16 as both normal and effective, with its Intelligence/proficiency formula and SRD page 104 reference.

The current-resource dialog recorded zero remaining Arcane Recovery uses. Native Save As wrote `output/complete-qa/wizard-20-current.dnd.json`; reopening that file through the native Open panel preserved zero uses. Comparing the files confirmed that the saved copy differs from the original fixture only in `resources["wizard:arcane-recovery"]`, and that the original fixture's resources remain empty. The latest live sheet was also scrolled across the feature-to-feat transition to confirm that print page-break rules do not create gaps on screen.

The final level-20 Wizard and Sorcerer exports were rendered and checked on every page: five Wizard pages and six Sorcerer pages. An additional five-page Wizard override example visibly marks maximum HP 165 and prints normal HP 162, effective HP 165, the required reason, and the underlying calculation. All three exports retain edition/module identification, source references, and the SRD license attribution. Their character geometry remained inside the page margins, and no blank pages, clipped fields, crowded spell-column headings, or orphaned feat-section headings remained after the print-layout corrections.

The generated QA files are under `output/complete-qa/`; `pdf-geometry.json` records the final page checks and hashes, and `native-validation.json` records the native workflow checks. `tests/test_gui.cpp` covers the corresponding version, source, migration, dice, and resource contracts. The public full-character fixtures in `tests/test_srd55_complete.cpp` separately require complete evaluation for all twelve classes at every level from 1 through 20; these are baseline characters, not exhaustive combinations of every optional choice.
