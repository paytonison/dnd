# App icon

The app icon is adapted from **Welsh Dragon (Y Ddraig Goch)** by **Sodacan**, dated 6 February 2012, supplied by the user as a Wikimedia Commons PNG thumbnail.

Source: https://commons.wikimedia.org/wiki/File:Welsh_Dragon_(Y_Ddraig_Goch).svg

The source offers a choice of licenses. This project uses **Creative Commons Attribution-ShareAlike 3.0 Unported** for the source and the adapted icon assets:
https://creativecommons.org/licenses/by-sa/3.0/

Changes: AI-assisted adaptation into a square transparent app-icon composition, followed by size resampling and native ICNS/ICO encoding. The complete red dragon, heraldic pose, wing and tail motif remain recognizable. Sodacan does not endorse this application.

This license applies to the icon artwork and adaptations. Application code retains the repository's BSD-3-Clause license.

The approved master is `app-icon-master.png`; `app-icon.png` is the 1024-pixel runtime resource. `scripts/build-icons.sh` encodes the PNG into a multi-resolution macOS `.icns` and Windows `.ico`. The original user file is unchanged.

Built-in image generation was used. Final selected edit prompt:

> Production app icon cutout. Edit the supplied Welsh dragon image into a SQUARE canvas with the exact original red dragon preserved, complete full body visible, centered with about 8 percent transparent padding. The dragon itself is the icon. No white tile, no square background, no plaque, no shadow, no surrounding scene. True transparent alpha outside the RED DRAGON SILHOUETTE and inside its open gaps. Keep the original red/maroon flat colors, linework, eye, left-facing head, complete wings, feet and curled tail. Preserve the source shape instead of drawing a new dragon. Clean, smooth, precisely antialiased cutout edges. Do not draw a transparency checkerboard. A single isolated red dragon icon on actual transparency.
