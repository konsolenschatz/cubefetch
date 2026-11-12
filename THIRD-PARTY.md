# Third-party components

CubeFetch's own code is MIT-licensed (see [LICENSE](LICENSE)). The following
parts come from others and keep their own licenses.

| Component | Used for | License |
|---|---|---|
| DejaVu Sans / Sans Bold (`data/fonts/`) | UI font, embedded in the DOL | Bitstream Vera / DejaVu license, see `data/fonts/LICENSE-DejaVu.txt` |
| M PLUS 1p (`data/extra-fonts/`) | Japanese glyphs, loaded from the card | SIL Open Font License 1.1, see `data/extra-fonts/OFL-MPLUS.txt` |
| libogc2 | GameCube runtime, linked in | see the libogc2 project |
| FreeType | font rendering, linked in | FreeType License (FTL) — portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved. |
| libpng, zlib | cover decoding, database unpacking | libpng license, zlib license |
| GameTDB | cover art and game info, fetched at runtime, not shipped | see gametdb.com |

## Brand assets

The name "Konsolenschatz" and the Konsolenschatz coin (`data/brand/`,
`source/platform/ks_coin.h`) are the channel's brand and are **not** covered
by the MIT license. Forks should replace them with their own artwork.

"Nintendo" and "GameCube" are trademarks of Nintendo. CubeFetch is not
affiliated with Nintendo.
