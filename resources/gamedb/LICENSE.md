# Game data

`snes.tsv`, `gba.tsv` and `gbc.tsv` are derived from the libretro-database project.

- Source: https://github.com/libretro/libretro-database (`metadat/no-intro`, `genre`, `developer`, `publisher`, `releaseyear`, `maxusers`, `franchise`)
- Commit: c4e17d41ec1bb744fb064f7cefeb61b878fc5d64
- License: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0), https://creativecommons.org/licenses/by-sa/4.0/
- Changes: the DAT files were converted to a single tab-separated file, joined by ROM CRC32. Only the format changed; no values were edited. Regenerate with `python scripts/gamedb.py <commit> resources/gamedb/snes.tsv`.

This file remains available under the same license, CC BY-SA 4.0.
