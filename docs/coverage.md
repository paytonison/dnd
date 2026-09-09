# Coverage ledger

Coverage is recorded by publication and feature. A catalog entry is not an implementation claim. The delivery gates are evaluated against `PLAN.md`; this ledger must not redefine that scope to match whichever tests currently pass.

| Publication/profile | Current implementation | Verification |
| --- | --- | --- |
| Moldvay Basic (1981) | Implemented: seven classes, abilities, equipment, combat, spell selection and memorization | Original preview and full original pages inspected; independent acceptance suite passes; see `sources-bx.md` |
| Cook/Marsh Expert (1981) | B/X progression through printed tables: humans 14, dwarf 12, elf 10, halfling 8 | Printed-page references and independent tests at every supported class level pass |
| SRD 5.2.1 | Implemented experimental fighter and wizard levels 1–3, origins, equipment and spell choices | Official CC BY source; complete class/species/background acceptance passes; see `sources-srd55.md` |
| SRD 5.2.1 expanded module 2.0.0 | All 12 classes/subclasses through 20, full spell catalog, ordered class gains, feat/feature capacities and current effects | Complete public fixtures for all 240 single-class states pass; physical Wizard book lifecycle has focused and native acceptance; full lifecycle still in progress, see `v2-progress.md` |
| Original D&D | Planned | Not implemented or verified |
| Holmes Basic | Planned | Not implemented or verified |
| AD&D 1e | Planned | Not implemented or verified |
| BECMI / Rules Cyclopedia | Planned, distinct profiles | Not implemented or verified |
| AD&D 2e | Planned | Not implemented or verified |
| D&D 3.0 | Planned | Not implemented or verified |
| D&D 3.5 | Planned | Not implemented or verified |
| D&D 4e | Planned | Not implemented or verified |
| 2014 5e / SRD 5.1 | Planned | Not implemented or verified |
| Supplemental publications | Per-edition expansion pending; common pack import interface implemented | No supplemental publication lifecycle is claimed as implemented |

The full B/X human tables stop at level 14, but Expert X7–X8 expressly allows human advancement through 36. The interface now labels 14 as the **printed-table coverage limit**, while dwarf 12, elf 10, and halfling 8 remain actual class limits. Beyond-14 XP/HP increments and attack guidance are available in the original book; later spells and special abilities require an explicit continuation profile and campaign decisions. That profile is not yet implemented, so the original first-release normal-maximum requirement remains **open**. No later edition or speculative complete progression is substituted. See the precise source boundary in `sources-bx.md`.

On September 8, 2026, the user explicitly chose to **keep levels above 14 unavailable for now**. Human continuation is therefore deferred while work proceeds on the remaining foundations and lifecycle. This scope decision does not change the original source's human maximum or mark continuation implemented.

Platform claims likewise remain separate: CI jobs are configured for macOS, Windows and Linux. A checked-in workflow is not proof that a hosted job ran. Initial local runtime and packaging acceptance are performed on macOS; unexecuted platforms remain unverified.
