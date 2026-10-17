The ring menus use three separate native assets. Replacement PNGs can be
attached together in a ZIP with these names and dimensions:

| Asset | Files for players 5–8 | Native size |
|---|---|---|
| Kart/player banner | `player-5.png` through `player-8.png` | 64 × 16 |
| Player-count badge | `count-5.png` through `count-8.png` | 32 × 16 |
| Confirmed-character marker | `marker-5.png` through `marker-8.png` | 16 × 16 |

Use RGBA PNG, transparent backgrounds, no upscaling or antialiasing, and at
most fifteen opaque colors per image. Keep the native borders, bevels and
baseline. Player colors are blue, orange, cyan and purple respectively.
Count badges should preserve the stock “-P” lettering and shaded numeral style.
Markers should preserve the stock square frame and use a clearly legible numeral.

The built-in 5–8 numerals are drawn at each asset's native size, with rounded
stems, shaded count digits and the original borders. Replacement clears the
entire old numeral and its halo, including the outer pixels of the stock 4,
so old horizontal strokes cannot remain beside the new digits.

Export private stock references with:

```sh
python3 tools/ring/export_menu_assets.py --rom baserom.gba --output /tmp/ring-menu-art
```

Pillow is required for export only. The game build does not require it.
The exported graphics come from the private ROM; do not add them to Git.
Attach replacement files or a ZIP in chat and they can be converted to native
4-bpp tiles and palettes. Enlarged previews are welcome alongside the originals.
