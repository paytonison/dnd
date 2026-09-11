# Application version and release policy

**Dungeoning a Dragon v1.0.0 is the basic, minimum version of the application.** This is the starting application baseline. Its supported and experimental rules coverage is recorded separately in the coverage ledger (`docs/coverage.md`); the version designation does not declare an incomplete system finished or waive the acceptance requirements in `PLAN.md`.

## Application releases

| Release type | Numbering | Meaning |
| --- | --- | --- |
| Minimum baseline | **v1.0.0** | The basic, minimum application from which development proceeds. |
| Major release | **vX.0.0** | A full implementation of a game system. After the initial baseline, a new major number marks a completed system implementation with its required source, rules, lifecycle and delivery acceptance. Partial system work does not by itself qualify. |
| Update | **v1.X.0** | Content additions and new features within the v1 series. The same minor-number convention applies within later major versions. |
| Patch | **v1.0.X** | Bug fixes, corrections and maintenance patches. The same patch-number convention applies to later updates and major versions. |

Examples: adding supported content or a new feature advances v1.0.0 to v1.1.0; correcting a defect in v1.1.0 advances it to v1.1.1; completing a system implementation is a major-release milestone such as v2.0.0. A major bump resets the minor and patch numbers to zero; an update resets the patch number to zero. These examples define the policy and do not announce scheduled releases or completed system coverage.

## Keep version identities separate

The application is **v1.0.0**, including when it loads a rules module whose independent version is `2.0.0`.

| Identity | Current examples | Purpose |
| --- | --- | --- |
| Application release | **v1.0.0** | Version of Dungeoning a Dragon as a whole; follows the release policy above. |
| Game edition/profile | B/X (1981), 5E (2014), 5.5E (2024) | Identifies the game system being implemented. |
| Source publication | SRD 5.1, SRD 5.2.1, Basic Rules 0.3 or 1.0 | Identifies the exact published reference; never renumbered to match the app. |
| Rules module | `bx@1.0.0`, `srd51@1.0.0`, `srd55@1.0.0`, `srd55@2.0.0` | Exact executable rules implementation pinned by a character. |
| Content pack | `srd51-core@1.0.0`, `srd55-core@2.0.0` | Exact content definitions and dependencies pinned by a character. |
| Save/schema format | `schemaVersion: 1` | Structure of saved documents or manifests. |

An application release does not automatically migrate a character, renumber modules or packs, replace a source edition, or change a save schema. Existing module and pack versions remain available as required for exact-version saves. Coverage continues to distinguish cataloged, partial, implemented and verified mechanics.

## Version source and documentation maintenance

`CMakeLists.txt` declares the application version with `project(DungeoningADragon VERSION 1.0.0 ...)`. CMake supplies that value to the desktop and CLI through `DND_APP_VERSION`, to the macOS bundle metadata, and to CPack. Do not maintain a separate hard-coded application version in C++.

The desktop About dialog and `dnd-cli --version` identify the application release. Character identity panes, edition menus and source dialogs identify rules-module or content-pack versions; those numbers are not application releases.

Authored application documentation states **v1.0.0**. The application guides and ledgers link to this policy; the policy and artwork attribution also remain readable as standalone packaged notices. Update that documentation identity together with the CMake application version when making a release under this policy. Source citations, dependency versions, licensed attribution text and exact module/pack pins retain their own identities.

Dated test reports, hashes, screenshots and archives describe the artifacts actually inspected at the time. Adding a current application-version notice to their documentation does not relabel or revalidate those older artifacts. A new version designation alone is not evidence of a completed system or a publicly published release.
