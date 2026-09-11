# Logo and app icon

Documentation for **Dungeoning a Dragon v1.0.0**, the basic, minimum application.

The logo and app icon are adapted from the user-supplied file **dragon-312035_1280.png**, a red seated dragon silhouette. Its SHA-256 is `d6687860c64ce3d1dd2f195a88eca1470bcf46773ef8e1a73e3650e992ef86b3`.

The matching [Pixabay source page](https://pixabay.com/vectors/dragon-red-symbol-fantasy-isolated-312035/) credits **Clker-Free-Vector-Images** and gives a published date of **August 3, 2014**. The supplied file's local download metadata points to Needpix, which mirrors this artwork. The matching name, silhouette, publication record, and download metadata establish the recorded provenance; the local file was not verified byte-for-byte against Pixabay's CDN.

[Section 4 of Pixabay's Terms of Service](https://pixabay.com/service/terms/) identifies content published before January 9, 2019 as **Creative Commons Zero (CC0)** content. This project's source-license record uses that provision and the artwork's 2014 published date. See the [CC0 1.0 public-domain dedication](https://creativecommons.org/publicdomain/zero/1.0/). Source-page and terms verification: September 11, 2026.

Changes: AI-assisted adaptation into a ruby-red glass dragon logo and a matching app icon on a pale frosted rounded tile, followed by user-authorized local transparency cleanup, size resampling, and native ICNS/ICO encoding. The generated checkerboard background was removed to produce actual transparent pixels. The source silhouette's seated pose, left-facing head, wing, and curled tail guide the design. The original user file is unchanged.

The macOS Golden Gate style uses glass highlights and refraction informed by [Apple's Icon Composer design guidance](https://developer.apple.com/icon-composer/). These are static raster assets; no adaptive Icon Composer `.icon` document is bundled. This artwork is not an Apple asset, and no endorsement by Apple, Pixabay, or the source contributor is implied.

Artwork provenance and the source's CC0 dedication are separate from the application code, which retains the repository's BSD-3-Clause license. The replaced Welsh Dragon artwork's CC BY-SA notice describes the earlier assets and does not apply to this new source.

In the source repository, `assets/branding/dragon-logo.png` is the standalone transparent logo; `assets/icons/app-icon-master.png` is the app-icon master; and `assets/icons/app-icon.png` is the 1024-pixel runtime resource. `scripts/build-icons.sh` encodes that resource into the multi-resolution macOS `app-icon.icns` and Windows `app-icon.ico`. `assets/branding/generation-prompts.json` records the generation prompts and processing provenance. These paths identify source-repository assets; this notice remains readable independently inside a packaged application.

The application remains **v1.0.0**, its basic, minimum version. This branding change does not alter game-system coverage, exact rules-module/content-pack identities, or saved-character formats.
