# Assets of the original

Audit of the licenses of the assets of `a327ex/SNKRX@6b93a64`, before
any of them is copied into this repository.

The original says: "All assets have their specific licenses and they
are linked to in the game's credits. All code is under the MIT
license." The credits are in `Arena:create_credits` (`arena.lua`).

## Inventory

| Folder | Files | Used by the game |
|--------|-------|------------------|
| `assets/sounds` | 114 `.ogg` | yes |
| `assets/images` | 111 `.png` | yes (109 `Image(...)` calls) |
| `assets/fonts` | 3 `.ttf` | `FatPixelFont` and `PixulBrush`, at size 8 |
| `assets/shaders` | 5 `.frag`, 1 `.vert` | yes |
| `assets/media` | 74 images | no: Steam pages, achievements, screenshots |

## Licenses

| Assets | Files | Author, as credited | License | State |
|--------|-------|---------------------|---------|-------|
| Music `Kubbi - Ember - *` | 7 | Kubbi, album "Ember" | not stated in the repository | to verify |
| Sounds `<id>__<user>__*` | 7 | freesound.org users: JustinBW, jcallison, Hybrid_V, womb_affliction, bajko, benzix2, InspectorJ | per sound, on freesound.org | to verify |
| Sound `bamboo_hit_by_lord` | 1 | "lord" (T_TGames) | not stated | to verify |
| Other sounds | 99 | Sidearm Studios, "Ultimate Sound FX Bundle" | commercial bundle: royalty free in a product; no term found that allows a copy of the raw files | not copied |
| Images (icons) | 111 | not in the credits | not stated | not copied |
| Fonts | 3 | not in the credits | not stated | not copied |
| Shaders | 6 | a327ex | part of the code (MIT) | may be copied |

The pages of freesound.org, kubbi.bandcamp.com, and itch.io are not
reachable from the container where this audit was made, so the rows
"to verify" stay open.

## Decision

- Nothing but the shaders is copied into this repository.
- The port loads the assets at run time from a checkout of the
  original, outside the repository, given by the variable
  `SNKRX_DIR`.
- A row moves to "copied" only after its license is read and allows
  it.
